#include "fonts.h"
#include "settings/settings.h"

/* U+23F5 BLACK MEDIUM RIGHT-POINTING TRIANGLE, one terminal cell. */
static const uint8_t term_symbol_bitmap[] = {0x40,0x6,0x0,0x70,0x7,0x80,0x7c,0x7,0xe0,0x7f,0x7,0xf8,0x7f,0x87,0xf0,0x7e,0x7,0xc0,0x78,0x7,0x0,0x60,0x4,0x0};
static const lv_font_fmt_txt_glyph_dsc_t term_symbol_glyphs[] = {
    {0}, {.bitmap_index=0, .adv_w=192, .box_w=12, .box_h=16, .ofs_x=0, .ofs_y=2}
};
static const lv_font_fmt_txt_cmap_t term_symbol_cmap[] = {
    {.range_start=0x23F5, .range_length=1, .glyph_id_start=1, .type=LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY}
};
static const lv_font_fmt_txt_dsc_t term_symbol_dsc = {
    .glyph_bitmap=term_symbol_bitmap, .glyph_dsc=term_symbol_glyphs,
    .cmaps=term_symbol_cmap, .cmap_num=1, .bpp=1
};
static lv_font_t s_term_symbols = {
    .get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt, .get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,
    .line_height=26, .base_line=5, .dsc=&term_symbol_dsc
};

/* The generated fonts live in flash (const). LVGL stores the fallback pointer
 * inside lv_font_t, so we work on RAM copies. */
static lv_font_t s_px20, s_px24, s_px36b, s_cjk, s_mont22, s_mont30, s_icons;
static bool s_terminal_pixel = true;
const lv_font_t *FONT_SMALL = &s_px20;
const lv_font_t *FONT_BODY = &s_px24;
const lv_font_t *FONT_TITLE = &s_px36b;
const lv_font_t *FONT_MONO = &s_px24;
lv_font_t g_term_font;
const lv_font_t *FONT_ICON = &font_icons32;

void fonts_set_pixel_icons(bool pixel)
{
    s_icons = font_icons16;
    FONT_ICON = pixel ? &font_icons32 : &lv_font_montserrat_30;
}

void fonts_select(bool pixel_theme, lang_t lang)
{
    bool chinese = lang == LANG_ZH_CN || lang == LANG_ZH_TW;
    bool pixel_text = pixel_theme && !chinese;
    s_terminal_pixel = pixel_text;
    fonts_set_pixel_icons(pixel_theme);

    /* Rebuild a finite fallback chain for the selected primary face. */
    s_px20.fallback = &s_cjk;
    s_px24.fallback = &s_cjk;
    s_px36b.fallback = &s_px24;
    s_cjk.fallback = &s_icons;
    if (pixel_text || lang != LANG_EN) {
        s_icons.fallback = &s_mont22;
        s_mont22.fallback = NULL;
        s_mont30.fallback = &s_cjk;
    } else {
        s_icons.fallback = NULL;
        s_mont22.fallback = &s_cjk;
        s_mont30.fallback = &s_cjk;
    }

    if (pixel_text) {
        FONT_SMALL = &s_px20;
        FONT_BODY = &s_px24;
        FONT_TITLE = &s_px36b;
    } else if (lang == LANG_EN) {
        /* Montserrat is compact and clear for Latin-only modern screens. */
        FONT_SMALL = &s_mont22;
        FONT_BODY = &s_mont22;
        FONT_TITLE = &s_mont30;
    } else {
        /* One Noto face for every Japanese/Chinese glyph prevents visible
         * weight, baseline and corner-style changes inside one sentence. */
        FONT_SMALL = &s_cjk;
        FONT_BODY = &s_cjk;
        FONT_TITLE = &s_cjk;
    }
    FONT_MONO = &g_term_font;
    fonts_set_term_line_height(g_settings.term_line_height ? g_settings.term_line_height : 32);
}

void fonts_init(void)
{
    s_px20 = font_px20; s_px24 = font_px24; s_px36b = font_px36b;
    s_cjk = font_cjk; s_mont22 = lv_font_montserrat_22; s_mont30 = lv_font_montserrat_30;
    /* MaruMinya covers Japanese, so the simplified and traditional Chinese
     * of the zh translations falls through to Noto, and the LVGL icon glyphs
     * to Montserrat behind it. Noto Sans Mono CJK at 24 px advances 12 px, the
     * same as MaruMinya, so a fallback glyph still fills one terminal cell. */
    /* The generated Noto font has generous source-font vertical metrics.
     * Normalize them for 24 px UI use while retaining its anti-aliased glyphs. */
    s_cjk.line_height = 28;
    s_cjk.base_line = 5;
    fonts_select(true, i18n_get_lang());
}

void fonts_set_term_line_height(int target)
{
    const lv_font_t *base = s_terminal_pixel ? &s_px24 : &s_cjk;
    g_term_font = *base;
    if (target < 28) target = 28;
    if (target > 48) target = 48;
    int32_t delta = (int32_t)base->line_height - target;   /* > 0 tightens */
    g_term_font.line_height = target;
    g_term_font.base_line = base->base_line - delta / 2;
    if (g_term_font.base_line < 0) g_term_font.base_line = 0;
    s_term_symbols.fallback = &s_cjk;
    g_term_font.fallback = &s_term_symbols;
}
