#pragma once
#include "lvgl.h"
#include "i18n/i18n.h"
#ifdef __cplusplus
extern "C" {
#endif
/* MaruMinya from the repository TTF, rasterized on exact 12px multiples.
 * Legacy C names are retained: font_px20 now uses the same 24px/2x grid
 * for readable secondary text. Missing CJK glyphs fall back to Noto. */
LV_FONT_DECLARE(font_clock192);
LV_FONT_DECLARE(font_px20);
LV_FONT_DECLARE(font_px24);
LV_FONT_DECLARE(font_px36b);
LV_FONT_DECLARE(font_cjk);
LV_FONT_DECLARE(font_icons16);
LV_FONT_DECLARE(font_icons32);
extern const lv_font_t *FONT_ICON;
void fonts_set_pixel_icons(bool pixel);
/* Select a coherent face for the current theme and language. Chinese always
 * uses the complete CJK face, because mixing missing MaruMinya glyphs inline
 * is much more distracting than changing the whole screen consistently. */
void fonts_select(bool pixel_theme, lang_t lang);

extern const lv_font_t *FONT_SMALL;   /* 24 px MaruMinya */
extern const lv_font_t *FONT_BODY;    /* 24 px pixel */
extern const lv_font_t *FONT_TITLE;   /* 36 px pixel, bold */
extern const lv_font_t *FONT_MONO;    /* 24 px pixel, 12 px cells (terminal) */
extern lv_font_t g_term_font;         /* FONT_MONO with an adjustable line height */

void fonts_init(void);
void fonts_set_term_line_height(int px);
#ifdef __cplusplus
}
#endif
