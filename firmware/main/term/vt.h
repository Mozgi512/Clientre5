#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define VT_ATTR_BOLD      0x01
#define VT_ATTR_UNDERLINE 0x02
#define VT_ATTR_INVERSE   0x04
#define VT_ATTR_WIDE      0x08   /* first cell of a double-width character */
#define VT_ATTR_WIDE_CONT 0x10   /* placeholder cell after a wide character */
#define VT_ATTR_DIM       0x20
#define VT_ATTR_ITALIC    0x40
#define VT_ATTR_STRIKE    0x80

/* Colour encoding: 0x80000000 = default colour flag, otherwise 0x00RRGGBB */
#define VT_COLOR_DEFAULT  0x80000000u

/* Replaces the 16 ANSI colours used for newly written cells. */
void vt_set_palette(const uint32_t pal[16]);

typedef struct {
    uint32_t cp;
    uint32_t fg;
    uint32_t bg;
    uint8_t attr;
} vt_cell_t;

typedef struct vt vt_t;

typedef void (*vt_write_cb_t)(const uint8_t *data, size_t len, void *user);  /* responses to host */
typedef void (*vt_bell_cb_t)(void *user);
typedef void (*vt_title_cb_t)(const char *title, void *user);

vt_t *vt_create(int cols, int rows, int scrollback);
void vt_destroy(vt_t *vt);
void vt_resize(vt_t *vt, int cols, int rows);
void vt_feed(vt_t *vt, const uint8_t *data, size_t len);
void vt_reset(vt_t *vt);

int vt_cols(const vt_t *vt);
int vt_rows(const vt_t *vt);
/* Row as currently displayed (taking the scrollback view offset into account). */
const vt_cell_t *vt_view_row(vt_t *vt, int row);
bool vt_row_dirty(vt_t *vt, int row);
void vt_clear_dirty(vt_t *vt);
void vt_mark_all_dirty(vt_t *vt);
void vt_cursor(const vt_t *vt, int *x, int *y, bool *visible);
int  vt_view_offset(const vt_t *vt);         /* 0 = live, >0 lines into history */
int  vt_scrollback_used(const vt_t *vt);
void vt_scroll_view(vt_t *vt, int delta);     /* + = older */
void vt_scroll_view_reset(vt_t *vt);
bool vt_app_cursor(const vt_t *vt);
bool vt_app_keypad(const vt_t *vt);
bool vt_bracketed_paste(const vt_t *vt);
bool vt_mouse_enabled(const vt_t *vt);
void vt_set_write_cb(vt_t *vt, vt_write_cb_t cb, void *user);
void vt_set_bell_cb(vt_t *vt, vt_bell_cb_t cb, void *user);
void vt_set_title_cb(vt_t *vt, vt_title_cb_t cb, void *user);
const char *vt_title(const vt_t *vt);
int vt_wcwidth(uint32_t cp);
/* Extract plain text of a row (for copy). */
size_t vt_row_text(vt_t *vt, int row, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
