#pragma once
#include "lvgl.h"

/* Screen-owned draw hooks; no offscreen framebuffer or input-blocking overlay. */
void ui_motion_attach(lv_obj_t *screen);
void ui_motion_load(lv_obj_t *screen, lv_screen_load_anim_t requested);
void ui_motion_start(lv_obj_t *screen, lv_obj_t *region, bool reverse);
