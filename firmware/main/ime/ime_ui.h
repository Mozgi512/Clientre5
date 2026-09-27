#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Floating preedit + candidate bar on the top layer. */
void ime_ui_init(void);
void ime_ui_set_anchor(int32_t bottom_y);   /* bar bottom edge in screen coords; <0 = screen bottom */
void ime_ui_refresh(void);
void ime_ui_hide(void);
void ime_ui_set_indicator(lv_obj_t *label);  /* label that shows "A/あ/ア" (may be NULL) */
#ifdef __cplusplus
}
#endif
