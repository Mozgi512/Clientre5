#include "ui.h"
#include "media/camera.h"
#include "storage/sd.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define PREVIEW_WIDTH 1280U
#define PREVIEW_HEIGHT 720U

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *image;
    lv_obj_t *status;
    lv_obj_t *shutter;
    lv_image_dsc_t descriptor;
    uint8_t *pixels;
    uint32_t sequence;
    camera_photo_state_t photo_state;
} camera_ui_t;

static camera_ui_t s_camera;

static void shutter_cb(lv_event_t *event)
{
    (void)event;
    if (!sd_is_mounted()) {
        ui_toast(tr(STR_TF_NOT_MOUNTED));
        return;
    }
    if (camera_take_photo()) {
        lv_label_set_text(s_camera.status, tr(STR_CAMERA_SAVING));
        lv_obj_add_state(s_camera.shutter, LV_STATE_DISABLED);
    }
}

static void preview_cb(lv_event_t *event) { shutter_cb(event); }

static void tick(lv_timer_t *timer)
{
    (void)timer;
    if (!s_camera.screen) return;
    camera_state_t state = camera_state();
    if (state == CAMERA_STATE_RUNNING) {
        uint32_t width = 0, height = 0;
        if (camera_copy_frame(s_camera.pixels, PREVIEW_WIDTH * PREVIEW_HEIGHT * 2,
                              &width, &height, &s_camera.sequence)) {
            s_camera.descriptor.header.w = width;
            s_camera.descriptor.header.h = height;
            s_camera.descriptor.header.stride = width * 2;
            s_camera.descriptor.data_size = width * height * 2;
            lv_obj_invalidate(s_camera.image);
        }
        if (s_camera.photo_state == CAMERA_PHOTO_IDLE)
            lv_label_set_text(s_camera.status, tr(STR_CAMERA_READY));
    } else if (state == CAMERA_STATE_ERROR) {
        const char *detail = camera_error();
        char message[160];
        snprintf(message, sizeof(message), "%s\n%s", tr(STR_CAMERA_FAILED), detail);
        lv_label_set_text(s_camera.status, message);
        lv_obj_add_state(s_camera.shutter, LV_STATE_DISABLED);
    }

    char path[320];
    camera_photo_state_t photo = camera_photo_state(path, sizeof(path));
    if (photo != s_camera.photo_state) {
        s_camera.photo_state = photo;
        if (photo == CAMERA_PHOTO_SAVED) {
            lv_label_set_text(s_camera.status, tr(STR_CAMERA_SAVED));
            ui_toast(tr(STR_CAMERA_SAVED));
            lv_obj_remove_state(s_camera.shutter, LV_STATE_DISABLED);
        } else if (photo == CAMERA_PHOTO_ERROR) {
            lv_label_set_text(s_camera.status, tr(STR_CAMERA_SAVE_FAIL));
            ui_toast(tr(STR_CAMERA_SAVE_FAIL));
            lv_obj_remove_state(s_camera.shutter, LV_STATE_DISABLED);
        }
    }
}

static void deleted(lv_event_t *event)
{
    (void)event;
    s_camera.screen = NULL;
    camera_stop();
    free(s_camera.pixels);
    memset(&s_camera, 0, sizeof(s_camera));
}

lv_obj_t *ui_camera_create(void *arg)
{
    (void)arg;
    memset(&s_camera, 0, sizeof(s_camera));
    lv_obj_t *screen = ui_screen_base();
    s_camera.screen = screen;
    lv_obj_add_event_cb(screen, deleted, LV_EVENT_DELETE, NULL);
    ui_topbar(screen, tr(STR_HOME_CAMERA), true);

    lv_obj_t *content = lv_obj_create(screen);
    lv_obj_set_pos(content, 0, UI_TOPBAR_H);
    lv_obj_set_size(content, LV_PCT(100), lv_display_get_vertical_resolution(NULL) - UI_TOPBAR_H);
    lv_obj_set_style_bg_color(content, lv_color_black(), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

    s_camera.pixels = calloc(PREVIEW_WIDTH * PREVIEW_HEIGHT, 2);
    if (s_camera.pixels) {
        s_camera.descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
        s_camera.descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
        s_camera.descriptor.header.w = PREVIEW_WIDTH;
        s_camera.descriptor.header.h = PREVIEW_HEIGHT;
        s_camera.descriptor.header.stride = PREVIEW_WIDTH * 2;
        s_camera.descriptor.data = s_camera.pixels;
        s_camera.descriptor.data_size = PREVIEW_WIDTH * PREVIEW_HEIGHT * 2;
        s_camera.image = lv_image_create(content);
        lv_image_set_src(s_camera.image, &s_camera.descriptor);
        int32_t available_h = lv_display_get_vertical_resolution(NULL) - UI_TOPBAR_H - 8;
        uint32_t scale = LV_MIN(256U, (uint32_t)available_h * 256U / PREVIEW_HEIGHT);
        lv_image_set_scale(s_camera.image, scale);
        lv_obj_center(s_camera.image);
        lv_obj_add_flag(s_camera.image, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_camera.image, preview_cb, LV_EVENT_SHORT_CLICKED, NULL);
    }

    s_camera.status = lv_label_create(content);
    lv_label_set_text(s_camera.status, tr(STR_CAMERA_STARTING));
    lv_obj_set_style_text_font(s_camera.status, FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_camera.status, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_camera.status, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_camera.status, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(s_camera.status, 12, 0);
    lv_obj_set_style_pad_ver(s_camera.status, 7, 0);
    lv_obj_set_style_radius(s_camera.status, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(s_camera.status, LV_ALIGN_TOP_LEFT, 14, 14);

    s_camera.shutter = lv_button_create(content);
    lv_obj_set_size(s_camera.shutter, 176, 58);
    lv_obj_align(s_camera.shutter, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_radius(s_camera.shutter, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_camera.shutter, UI_ACCENT, 0);
    lv_obj_add_event_cb(s_camera.shutter, shutter_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(s_camera.shutter);
    lv_label_set_text_fmt(label, LV_SYMBOL_IMAGE "  %s", tr(STR_CAMERA_CAPTURE));
    lv_obj_set_style_text_color(label, UI_BG, 0);
    lv_obj_center(label);

    if (!s_camera.pixels || !camera_start()) {
        lv_label_set_text(s_camera.status, tr(STR_CAMERA_FAILED));
        lv_obj_add_state(s_camera.shutter, LV_STATE_DISABLED);
    }
    ui_timer_bind(lv_timer_create(tick, 80, NULL), screen);
    return screen;
}
