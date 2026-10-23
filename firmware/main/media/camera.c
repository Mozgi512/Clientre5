#include "camera.h"

#include "bsp/m5stack_tab5.h"
#include "driver/jpeg_encode.h"
#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_video_device.h"
#include "esp_video_ioctl.h"
#include "esp_video_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "storage/sd.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

#define CAMERA_WIDTH 1280U
#define CAMERA_HEIGHT 720U
#define CAMERA_BUFFER_COUNT 2

static const char *TAG = "camera";
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static volatile bool s_stop;
static volatile bool s_photo_request;
static camera_state_t s_state;
static camera_photo_state_t s_photo_state;
static uint8_t *s_latest;
static size_t s_latest_size;
static uint32_t s_width, s_height, s_sequence;
static volatile unsigned s_rotation;       /* quarter turns counter-clockwise */
static ppa_client_handle_t s_ppa;
static char s_error[96];
static char s_photo_path[320];

static void set_error(const char *message)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_error, message, sizeof(s_error));
    s_state = CAMERA_STATE_ERROR;
    if (s_lock) xSemaphoreGive(s_lock);
    ESP_LOGE(TAG, "%s", message);
}

static const ppa_srm_rotation_angle_t s_ppa_angles[4] = {
    PPA_SRM_ROTATION_ANGLE_0, PPA_SRM_ROTATION_ANGLE_90,
    PPA_SRM_ROTATION_ANGLE_180, PPA_SRM_ROTATION_ANGLE_270,
};

/* Copies a sensor frame through the PPA, turned by `rotation` quarter turns
 * counter-clockwise and optionally byte-swapped. dst must be cache aligned.
 * The Tab5's sensor delivers a left-right mirror image, so the frame is also
 * mirrored back. The PPA mirrors after it rotates, in output coordinates
 * (measured on the device), so the sensor's x axis is the output's y axis
 * after a quarter or three-quarter turn. */
static bool ppa_turn(const uint8_t *src, uint32_t width, uint32_t height, unsigned rotation,
                     bool byte_swap, void *dst, size_t dst_size)
{
    bool odd = rotation & 1;
    ppa_srm_oper_config_t op = {
        .in = {
            .buffer = src, .pic_w = width, .pic_h = height,
            .block_w = width, .block_h = height, .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = dst, .buffer_size = dst_size,
            .pic_w = odd ? height : width, .pic_h = odd ? width : height,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = s_ppa_angles[rotation & 3],
        .scale_x = 1.0f, .scale_y = 1.0f,
        .mirror_x = !odd, .mirror_y = odd,
        .byte_swap = byte_swap,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    return s_ppa && ppa_do_scale_rotate_mirror(s_ppa, &op) == ESP_OK;
}

static bool save_jpeg(const uint8_t *rgb, uint32_t width, uint32_t height, unsigned rotation)
{
    if (!sd_is_mounted() || !fs_mkdir_p(SD_MOUNT "/DCIM/Clientre5")) return false;

    jpeg_encoder_handle_t encoder = NULL;
    jpeg_encode_engine_cfg_t engine_cfg = { .intr_priority = 0, .timeout_ms = 1000 };
    if (jpeg_new_encoder_engine(&engine_cfg, &encoder) != ESP_OK) return false;

    /* The JPEG engine reads RGB565 big-endian, while the ISP writes it
     * little-endian for LVGL: unswapped, a photo comes out red. The PPA swaps
     * the bytes and turns the photo upright in one pass. */
    bool odd = rotation & 1;
    uint32_t out_w = odd ? height : width, out_h = odd ? width : height;
    size_t frame_size = (size_t)width * height * 2, in_allocated = 0, allocated = 0;
    jpeg_encode_memory_alloc_cfg_t in_cfg = { .buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER };
    uint8_t *input = jpeg_alloc_encoder_mem(frame_size, &in_cfg, &in_allocated);
    bool ok = input && ppa_turn(rgb, width, height, rotation, true, input, in_allocated);

    jpeg_encode_cfg_t cfg = {
        .height = out_h,
        .width = out_w,
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV422,
        .image_quality = 86,
    };
    jpeg_encode_memory_alloc_cfg_t out_cfg = { .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER };
    uint8_t *jpeg = ok ? jpeg_alloc_encoder_mem(frame_size * 3 / 4, &out_cfg, &allocated) : NULL;
    uint32_t jpeg_size = 0;
    ok = jpeg && jpeg_encoder_process(encoder, &cfg, input, frame_size, jpeg, allocated, &jpeg_size) == ESP_OK;
    free(input);

    char path[sizeof(s_photo_path)] = "";
    if (ok) {
        fs_unique_name(SD_MOUNT "/DCIM/Clientre5", "IMG.jpg", path, sizeof(path));
        ok = fs_write_all(path, jpeg, jpeg_size);
    }
    free(jpeg);
    jpeg_del_encoder_engine(encoder);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (ok) strlcpy(s_photo_path, path, sizeof(s_photo_path));
    s_photo_state = ok ? CAMERA_PHOTO_SAVED : CAMERA_PHOTO_ERROR;
    xSemaphoreGive(s_lock);
    return ok;
}

static void camera_task(void *arg)
{
    (void)arg;
    int fd = -1;
    bool streaming = false;
    unsigned frame_counter = 0;
    uint8_t *buffers[CAMERA_BUFFER_COUNT] = {0};
    size_t lengths[CAMERA_BUFFER_COUNT] = {0};
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (bsp_camera_start(NULL) != ESP_OK) {
        set_error("SC202CS was not detected");
        goto done;
    }
    fd = open(BSP_CAMERA_DEVICE, O_RDONLY);
    if (fd < 0) { set_error("Could not open /dev/video0"); goto done; }

    struct v4l2_format format = {
        .type = type,
        .fmt.pix = {
            .width = CAMERA_WIDTH,
            .height = CAMERA_HEIGHT,
            .pixelformat = V4L2_PIX_FMT_RGB565,
        },
    };
    if (ioctl(fd, VIDIOC_S_FMT, &format) != 0 ||
        format.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565) {
        set_error("RGB565 camera preview is unavailable");
        goto done;
    }

    struct timeval timeout = { .tv_sec = 0, .tv_usec = 250000 };
    (void)ioctl(fd, VIDIOC_S_DQBUF_TIMEOUT, &timeout);
    struct v4l2_requestbuffers req = {
        .count = CAMERA_BUFFER_COUNT,
        .type = type,
        .memory = V4L2_MEMORY_MMAP,
    };
    if (ioctl(fd, VIDIOC_REQBUFS, &req) != 0 || req.count < CAMERA_BUFFER_COUNT) {
        set_error("Could not allocate camera buffers");
        goto done;
    }
    for (unsigned i = 0; i < CAMERA_BUFFER_COUNT; i++) {
        struct v4l2_buffer buffer = {
            .type = type, .memory = V4L2_MEMORY_MMAP, .index = i,
        };
        if (ioctl(fd, VIDIOC_QUERYBUF, &buffer) != 0) {
            set_error("Could not query camera buffer");
            goto done;
        }
        lengths[i] = buffer.length;
        buffers[i] = mmap(NULL, buffer.length, PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd, buffer.m.offset);
        if (buffers[i] == MAP_FAILED || ioctl(fd, VIDIOC_QBUF, &buffer) != 0) {
            set_error("Could not map camera buffer");
            goto done;
        }
    }
    ppa_client_config_t ppa_cfg = { .oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = 1 };
    if (ppa_register_client(&ppa_cfg, &s_ppa) != ESP_OK) { set_error("Could not start the PPA"); goto done; }
    const uint32_t sensor_w = format.fmt.pix.width, sensor_h = format.fmt.pix.height;
    s_latest_size = (size_t)sensor_w * sensor_h * 2;
    /* The PPA writes it, so it must start and end on cache lines. */
    s_latest = heap_caps_aligned_calloc(128, 1, s_latest_size,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_CACHE_ALIGNED);
    if (!s_latest) { set_error("Not enough memory for camera preview"); goto done; }
    s_width = sensor_w;
    s_height = sensor_h;
    if (ioctl(fd, VIDIOC_STREAMON, &type) != 0) {
        set_error("Could not start camera stream");
        goto done;
    }
    streaming = true;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state = CAMERA_STATE_RUNNING;
    s_error[0] = 0;
    xSemaphoreGive(s_lock);

    while (!s_stop) {
        struct v4l2_buffer buffer = { .type = type, .memory = V4L2_MEMORY_MMAP };
        if (ioctl(fd, VIDIOC_DQBUF, &buffer) != 0) continue;
        if ((buffer.flags & V4L2_BUF_FLAG_DONE) && buffer.index < CAMERA_BUFFER_COUNT) {
            size_t bytes = buffer.bytesused < s_latest_size ? buffer.bytesused : s_latest_size;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            /* The sensor runs at 30 fps, while the 80 ms UI refresh only needs
             * about 12 fps. Publishing every other frame avoids needless
             * full-frame PSRAM traffic without making the preview feel slow. */
            unsigned rotation = s_rotation & 3;
            if ((++frame_counter & 1U) == 0) {
                /* The PPA turns the frame upright for the current UI rotation. */
                if (bytes == s_latest_size &&
                    ppa_turn(buffers[buffer.index], sensor_w, sensor_h, rotation, false, s_latest, s_latest_size)) {
                    s_width = (rotation & 1) ? sensor_h : sensor_w;
                    s_height = (rotation & 1) ? sensor_w : sensor_h;
                } else {
                    memcpy(s_latest, buffers[buffer.index], bytes);
                    s_width = sensor_w;
                    s_height = sensor_h;
                }
                s_sequence++;
            }
            bool take_photo = s_photo_request;
            s_photo_request = false;
            xSemaphoreGive(s_lock);
            if (take_photo) save_jpeg(buffers[buffer.index], sensor_w, sensor_h, rotation);
        }
        if (ioctl(fd, VIDIOC_QBUF, &buffer) != 0) break;
    }

done:
    if (streaming) (void)ioctl(fd, VIDIOC_STREAMOFF, &type);
    for (unsigned i = 0; i < CAMERA_BUFFER_COUNT; i++)
        if (buffers[i] && buffers[i] != MAP_FAILED) munmap(buffers[i], lengths[i]);
    if (fd >= 0) close(fd);
    if (s_ppa) { ppa_unregister_client(s_ppa); s_ppa = NULL; }
    esp_video_deinit();
    (void)bsp_feature_enable(BSP_FEATURE_CAMERA, false);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    free(s_latest);
    s_latest = NULL;
    s_latest_size = 0;
    if (s_state != CAMERA_STATE_ERROR) s_state = CAMERA_STATE_OFF;
    s_task = NULL;
    xSemaphoreGive(s_lock);
    vTaskDelete(NULL);
}

void camera_set_rotation(unsigned ccw_quarter_turns) { s_rotation = ccw_quarter_turns & 3; }

bool camera_start(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_task) { xSemaphoreGive(s_lock); return true; }
    s_stop = false;
    s_photo_request = false;
    s_photo_state = CAMERA_PHOTO_IDLE;
    s_photo_path[0] = 0;
    s_error[0] = 0;
    s_state = CAMERA_STATE_STARTING;
    BaseType_t ok = xTaskCreatePinnedToCore(camera_task, "camera", 8192, NULL, 5, &s_task, 1);
    if (ok != pdPASS) { s_task = NULL; s_state = CAMERA_STATE_ERROR; strlcpy(s_error, "Could not create camera task", sizeof(s_error)); }
    xSemaphoreGive(s_lock);
    return ok == pdPASS;
}

void camera_stop(void)
{
    s_stop = true;
    for (unsigned i = 0; i < 80 && s_task; i++) vTaskDelay(pdMS_TO_TICKS(10));
}

camera_state_t camera_state(void)
{
    camera_state_t state;
    if (!s_lock) return CAMERA_STATE_OFF;
    xSemaphoreTake(s_lock, portMAX_DELAY); state = s_state; xSemaphoreGive(s_lock);
    return state;
}

const char *camera_error(void) { return s_error; }

bool camera_copy_frame(void *dst, size_t capacity, uint32_t *width,
                       uint32_t *height, uint32_t *sequence)
{
    if (!s_lock || !dst) return false;
    bool copied = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_latest && capacity >= s_latest_size && (!sequence || *sequence != s_sequence)) {
        memcpy(dst, s_latest, s_latest_size);
        if (width) *width = s_width;
        if (height) *height = s_height;
        if (sequence) *sequence = s_sequence;
        copied = true;
    }
    xSemaphoreGive(s_lock);
    return copied;
}

bool camera_take_photo(void)
{
    if (!s_lock || !sd_is_mounted()) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = s_state == CAMERA_STATE_RUNNING && !s_photo_request &&
              s_photo_state != CAMERA_PHOTO_PENDING;
    if (ok) { s_photo_request = true; s_photo_state = CAMERA_PHOTO_PENDING; }
    xSemaphoreGive(s_lock);
    return ok;
}

camera_photo_state_t camera_photo_state(char *path, size_t path_len)
{
    if (!s_lock) return CAMERA_PHOTO_IDLE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    camera_photo_state_t state = s_photo_state;
    if (path && path_len) strlcpy(path, s_photo_path, path_len);
    xSemaphoreGive(s_lock);
    return state;
}
