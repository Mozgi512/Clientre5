#pragma once
#include "lvgl.h"
#include "vt.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct term_view term_view_t;
term_view_t *term_view_create(lv_obj_t *parent, vt_t *vt, const lv_font_t *font);
void term_view_destroy(term_view_t *tv);
lv_obj_t *term_view_obj(term_view_t *tv);
void term_view_set_vt(term_view_t *tv, vt_t *vt);
/* Recompute cols/rows from the widget size; returns true if size changed. */
bool term_view_fit(term_view_t *tv, int *cols, int *rows);
void term_view_refresh(term_view_t *tv);      /* invalidate dirty rows */
void term_view_set_colors(term_view_t *tv, lv_color_t fg, lv_color_t bg);
typedef enum { TERM_CURSOR_BLOCK = 0, TERM_CURSOR_UNDERLINE = 1, TERM_CURSOR_BAR = 2 } term_cursor_t;
void term_view_set_cursor(term_view_t *tv, term_cursor_t style, bool blink, lv_color_t color);
int  term_view_cell_w(term_view_t *tv);
int  term_view_cell_h(term_view_t *tv);
/* True only while the current press has stayed within the tap slop. */
bool term_view_long_press_allowed(term_view_t *tv);
#ifdef __cplusplus
}
#endif
