#pragma once
#include "lvgl.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Decodes a JPEG into an RGB565 image descriptor sized to fit max_w x max_h
 * (hardware decoder for small images, scaled software decoder otherwise).
 * Free with image_loader_free(). Fills status text describing the path taken. */
lv_image_dsc_t *image_loader_jpeg(const char *path, int max_w, int max_h, char *status, int status_cap);
/* Decodes a PNG into an RGB565 image descriptor sized to fit max_w x max_h,
 * inflating and box-averaging scanline by scanline so a multi-megapixel file
 * needs no full-size intermediate buffer. NULL for interlaced or unsupported
 * files, which the caller can leave to LVGL's own decoder. */
lv_image_dsc_t *image_loader_png(const char *path, int max_w, int max_h, char *status, int status_cap);
void image_loader_free(lv_image_dsc_t *dsc);
/* Decode from memory (album art). */
lv_image_dsc_t *image_loader_jpeg_mem(const uint8_t *data, size_t len, int max_w, int max_h);
#ifdef __cplusplus
}
#endif
