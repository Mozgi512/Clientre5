#include "screenshot.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lcd_touch.h"
#include "bsp/m5stack_tab5.h"
#include "storage/sd.h"
#include "ui/ui.h"

static const char *TAG = "shot";
static esp_lcd_touch_handle_t s_tp = NULL;
static bool s_armed = true;

/* The BSP keeps the touch handle private; we get it from the LVGL indev driver data. */
static void find_touch_handle(void)
{
    lv_indev_t *indev = bsp_display_get_input_dev();
    if (!indev) return;
    /* esp_lvgl_port stores lvgl_port_touch_ctx_t* in driver data; its first member is the handle. */
    void *drv = lv_indev_get_driver_data(indev);
    if (drv) s_tp = *(esp_lcd_touch_handle_t *)drv;
}

static bool write_bmp(const char *path, lv_draw_buf_t *buf)
{
    uint32_t w = buf->header.w, h = buf->header.h, stride = buf->header.stride;
    lv_color_format_t cf = buf->header.cf;
    int bpp = lv_color_format_get_bpp(cf) / 8;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    uint32_t row_bytes = (w * 3 + 3) & ~3u;
    uint32_t data = row_bytes * h;
    uint8_t hdr[54] = { 'B', 'M' };
    uint32_t fsz = 54 + data;
    memcpy(hdr + 2, &fsz, 4); uint32_t off = 54; memcpy(hdr + 10, &off, 4);
    uint32_t ih = 40; memcpy(hdr + 14, &ih, 4); memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &h, 4);
    uint16_t planes = 1, bits = 24; memcpy(hdr + 26, &planes, 2); memcpy(hdr + 28, &bits, 2);
    memcpy(hdr + 34, &data, 4);
    fwrite(hdr, 1, 54, f);
    uint8_t *row = malloc(row_bytes);
    memset(row, 0, row_bytes);
    for (int y = (int)h - 1; y >= 0; y--) {
        const uint8_t *src = (const uint8_t *)buf->data + y * stride;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t r, g, b;
            if (bpp == 2) { uint16_t p = ((const uint16_t *)src)[x]; r = ((p >> 11) & 31) << 3; g = ((p >> 5) & 63) << 2; b = (p & 31) << 3; }
            else if (bpp == 3) { b = src[x * 3]; g = src[x * 3 + 1]; r = src[x * 3 + 2]; }
            else { b = src[x * 4]; g = src[x * 4 + 1]; r = src[x * 4 + 2]; }
            row[x * 3] = b; row[x * 3 + 1] = g; row[x * 3 + 2] = r;
        }
        fwrite(row, 1, row_bytes, f);
    }
    free(row);
    fclose(f);
    return true;
}

bool screenshot_take(char *out_path, int cap)
{
    if (!sd_is_mounted()) return false;
    fs_mkdir_p(SD_MOUNT "/ScreenShots");
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    char path[96];
    strftime(path, sizeof(path), SD_MOUNT "/ScreenShots/%Y%m%d_%H%M%S.bmp", &tm);
    lv_draw_buf_t *buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    if (!buf) { ESP_LOGE(TAG, "snapshot failed"); return false; }
    bool ok = write_bmp(path, buf);
    lv_draw_buf_destroy(buf);
    if (ok && out_path) strlcpy(out_path, path, cap);
    ESP_LOGI(TAG, "screenshot %s: %s", path, ok ? "ok" : "failed");
    return ok;
}

static void poll_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_tp) { find_touch_handle(); if (!s_tp) return; }
    uint16_t x[5], y[5], s[5]; uint8_t cnt = 0;
    /* data is refreshed by the LVGL port's own read callback */
    bool got = esp_lcd_touch_get_coordinates(s_tp, x, y, s, &cnt, 5);
    if (got && cnt >= 3) {
        if (s_armed) {
            s_armed = false;
            char p[96];
            if (screenshot_take(p, sizeof(p))) ui_toast(tr(STR_SCREENSHOT_SAVED)); else ui_toast(tr(STR_SCREENSHOT_FAILED));
        }
    } else if (!got || cnt == 0) {
        s_armed = true;
    }
}

void screenshot_init(void)
{
    find_touch_handle();
    lv_timer_create(poll_cb, 60, NULL);
}
