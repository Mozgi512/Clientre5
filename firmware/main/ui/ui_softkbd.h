#pragma once
#include "lvgl.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* On-screen keyboard that feeds keyboard_inject() (so the IME applies). */
lv_obj_t *softkbd_create(lv_obj_t *parent);
void softkbd_attach_textarea(lv_obj_t *ta, lv_obj_t *parent);  /* show when ta focused, hide on ready/defocus */
void softkbd_hide_all(void);
int  softkbd_height(void);
bool softkbd_visible(void);
#ifdef __cplusplus
}
#endif
