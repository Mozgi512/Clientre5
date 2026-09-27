/* Appearance: system theme (colours, corner rounding, spacing) and the SSH
 * terminal theme (colours, ANSI palette, cursor, line height). */
#include "ui.h"
#include "ui_theme.h"
#include "settings/settings.h"
#include "fonts/fonts.h"
#include "term/vt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ helpers */
static void hex_row_set(lv_obj_t *row, uint32_t rgb)
{
    lv_obj_t *val = lv_obj_get_user_data(row);
    if (val) lv_obj_set_style_text_color(val, UI_TEXT, 0);
    lv_obj_t *chip = lv_obj_create(row);
    lv_obj_set_size(chip, 24, 24);
    lv_obj_set_style_bg_color(chip, lv_color_hex(rgb), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, UI_MUTED, 0);
    lv_obj_set_style_radius(chip, ui_theme_radius(4), 0);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

static lv_obj_t *color_row(lv_obj_t *parent, const char *label, uint32_t rgb, lv_event_cb_t cb, void *ud)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "#%06lX", (unsigned long)(rgb & 0xFFFFFF));
    lv_obj_t *r = ui_setting_row(parent, label, buf, cb, ud);
    hex_row_set(r, rgb);
    return r;
}

/* Accepts "#RRGGBB", "RRGGBB" or "0xRRGGBB"; -1 when it is not a colour. */
static long parse_hex_color(const char *t)
{
    if (!t) return -1;
    while (*t == ' ') t++;
    if (*t == '#') t++;
    else if (t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t += 2;
    char *end = NULL;
    long v = strtol(t, &end, 16);
    if (!end || end == t || v < 0 || v > 0xFFFFFF) return -1;
    return v;
}

/* ------------------------------------------------------------ system theme */
static void preset_menu_cb(int i, void *ud)
{
    (void)ud;
    if (i < ui_theme_preset_count()) { ui_theme_set_preset(i); ui_theme_apply(); }
}
static void preset_cb(lv_event_t *e)
{
    (void)e;
    const char *items[12];
    int n = ui_theme_preset_count();
    if (n > 12) n = 12;
    for (int i = 0; i < n; i++) items[i] = ui_theme_preset_name(i);
    ui_menu(tr(STR_TH_PRESET), items, n, preset_menu_cb, NULL);
}

static void radius_cb(lv_event_t *e)
{
    g_settings.theme_radius = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(e));
    ui_theme_apply();
}

static void density_menu_cb(int i, void *ud)
{
    (void)ud;
    g_settings.theme_density = (uint8_t)i;
    ui_theme_apply();
}
static void density_cb(lv_event_t *e)
{
    (void)e;
    const char *items[3] = { tr(STR_TH_COMPACT), tr(STR_TH_NORMAL), tr(STR_TH_ROOMY) };
    ui_menu(tr(STR_TH_DENSITY), items, 3, density_menu_cb, NULL);
}

static void glow_cb(lv_event_t *e)
{
    g_settings.theme_glow = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    ui_theme_apply();
}

static void scanlines_cb(lv_event_t *e)
{
    g_settings.theme_scanlines = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    ui_theme_apply();
}

static void tile_accent_cb(lv_event_t *e)
{
    g_settings.theme_tile_accent = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    ui_theme_apply();
}

static void sys_color_typed(const char *text, void *ud)
{
    long v = parse_hex_color(text);
    if (v < 0) { ui_toast(tr(STR_BAD_COLOR)); return; }
    ui_theme_set_color((ui_color_id_t)(uintptr_t)ud, (uint32_t)v);
    ui_theme_apply();
}
static void sys_color_cb(lv_event_t *e)
{
    ui_color_id_t id = (ui_color_id_t)(uintptr_t)lv_event_get_user_data(e);
    char cur[16];
    snprintf(cur, sizeof(cur), "#%06lX", (unsigned long)ui_theme_color_hex(id));
    ui_prompt(ui_theme_color_name(id), "#RRGGBB", cur, false, sys_color_typed, (void *)(uintptr_t)id);
}

/* ------------------------------------------------------------ terminal theme */
static void term_save(void) { settings_save(); ui_nav_refresh(); }

static void term_color_typed(const char *text, void *ud)
{
    long v = parse_hex_color(text);
    if (v < 0) { ui_toast(tr(STR_BAD_COLOR)); return; }
    *(uint32_t *)ud = (uint32_t)v;
    term_save();
}
static void term_color_cb(lv_event_t *e)
{
    uint32_t *slot = lv_event_get_user_data(e);
    char cur[16];
    snprintf(cur, sizeof(cur), "#%06lX", (unsigned long)(*slot & 0xFFFFFF));
    ui_prompt(tr(STR_TH_TERMINAL), "#RRGGBB", cur, false, term_color_typed, slot);
}

static void cursor_menu_cb(int i, void *ud) { (void)ud; g_settings.term_cursor_style = (uint8_t)i; term_save(); }
static void cursor_cb(lv_event_t *e)
{
    (void)e;
    const char *items[3] = { tr(STR_TH_CUR_BLOCK), tr(STR_TH_CUR_UNDER), tr(STR_TH_CUR_BAR) };
    ui_menu(tr(STR_TH_CURSOR_STYLE), items, 3, cursor_menu_cb, NULL);
}
static void blink_cb(lv_event_t *e)
{
    g_settings.term_cursor_blink = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    settings_save();
}
static void lh_cb(lv_event_t *e)
{
    g_settings.term_line_height = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(e));
    fonts_set_term_line_height(g_settings.term_line_height);
    settings_save();
}
static void ansi_cb(lv_event_t *e) { (void)e; ui_nav_push(ui_ansi_create, NULL); }

static void reset_yes(bool yes, void *ud)
{
    (void)ud;
    if (!yes) return;
    app_settings_t keep = g_settings;       /* everything outside appearance */
    settings_reset_defaults();
    app_settings_t def = g_settings;        /* the appearance defaults we want back */
    g_settings = keep;
    g_settings.theme_preset = def.theme_preset;
    g_settings.theme_radius = def.theme_radius;
    g_settings.theme_density = def.theme_density;
    g_settings.theme_tile_accent = def.theme_tile_accent;
    g_settings.theme_glow = def.theme_glow;
    g_settings.theme_scanlines = def.theme_scanlines;
    memcpy(g_settings.theme_colors, def.theme_colors, sizeof(def.theme_colors));
    g_settings.term_fg = def.term_fg;
    g_settings.term_bg = def.term_bg;
    g_settings.term_cursor = def.term_cursor;
    g_settings.term_cursor_style = def.term_cursor_style;
    g_settings.term_cursor_blink = def.term_cursor_blink;
    g_settings.term_line_height = def.term_line_height;
    memcpy(g_settings.term_ansi, def.term_ansi, sizeof(def.term_ansi));
    fonts_set_term_line_height(g_settings.term_line_height);
    vt_set_palette(g_settings.term_ansi);
    ui_theme_apply();
}
static void reset_cb(lv_event_t *e) { (void)e; ui_confirm(tr(STR_TH_RESET), tr(STR_TH_RESET), reset_yes, NULL); }

/* ------------------------------------------------------------ screens */
static lv_obj_t *slider_card(lv_obj_t *parent, const char *label, int min, int max, int val, lv_event_cb_t cb)
{
    lv_obj_t *card = ui_card(parent);
    char b[64]; snprintf(b, sizeof(b), "%s: %d", label, val);
    ui_label(card, b, FONT_BODY, UI_TEXT);
    lv_obj_t *s = lv_slider_create(card);
    lv_obj_set_width(s, LV_PCT(96));
    lv_slider_set_range(s, min, max);
    lv_slider_set_value(s, val, LV_ANIM_OFF);
    lv_obj_add_event_cb(s, cb, LV_EVENT_RELEASED, NULL);
    return card;
}

lv_obj_t *ui_appearance_create(void *arg)
{
    (void)arg;
    lv_obj_t *scr = ui_screen_base();
    ui_topbar(scr, tr(STR_SET_THEME), true);
    lv_obj_t *c = ui_content(scr);

    uint8_t p = g_settings.theme_preset;
    ui_setting_row(c, tr(STR_TH_PRESET),
                   p < ui_theme_preset_count() ? ui_theme_preset_name(p) : tr(STR_TH_CUSTOM), preset_cb, NULL);
    slider_card(c, tr(STR_TH_RADIUS), 0, 24, g_settings.theme_radius, radius_cb);
    const char *dens[3] = { tr(STR_TH_COMPACT), tr(STR_TH_NORMAL), tr(STR_TH_ROOMY) };
    ui_setting_row(c, tr(STR_TH_DENSITY), dens[g_settings.theme_density <= 2 ? g_settings.theme_density : 1], density_cb, NULL);
    ui_switch_row(c, tr(STR_TH_TILE_ACCENT), g_settings.theme_tile_accent, tile_accent_cb, NULL);
    ui_switch_row(c, tr(STR_TH_GLOW), g_settings.theme_glow, glow_cb, NULL);
    ui_switch_row(c, tr(STR_TH_SCANLINES), g_settings.theme_scanlines, scanlines_cb, NULL);

    ui_section_title(c, tr(STR_TH_COLORS));
    for (int i = 0; i < UI_C_COUNT; i++)
        color_row(c, ui_theme_color_name((ui_color_id_t)i), ui_theme_color_hex((ui_color_id_t)i),
                  sys_color_cb, (void *)(uintptr_t)i);

    ui_section_title(c, tr(STR_TH_TERMINAL));
    color_row(c, tr(STR_TH_TERM_FG), g_settings.term_fg, term_color_cb, &g_settings.term_fg);
    color_row(c, tr(STR_TH_TERM_BG), g_settings.term_bg, term_color_cb, &g_settings.term_bg);
    color_row(c, tr(STR_TH_TERM_CURSOR), g_settings.term_cursor, term_color_cb, &g_settings.term_cursor);
    const char *cs[3] = { tr(STR_TH_CUR_BLOCK), tr(STR_TH_CUR_UNDER), tr(STR_TH_CUR_BAR) };
    ui_setting_row(c, tr(STR_TH_CURSOR_STYLE), cs[g_settings.term_cursor_style <= 2 ? g_settings.term_cursor_style : 0], cursor_cb, NULL);
    ui_switch_row(c, tr(STR_TH_CURSOR_BLINK), g_settings.term_cursor_blink, blink_cb, NULL);
    slider_card(c, tr(STR_TH_LINE_HEIGHT), 28, 40, g_settings.term_line_height, lh_cb);
    ui_setting_row(c, tr(STR_TH_ANSI), "", ansi_cb, NULL);

    ui_button_colored(c, tr(STR_TH_RESET), UI_CARD_HI, reset_cb, NULL);
    return scr;
}

/* ------------------------------------------------------------ ANSI palette */
static void ansi_typed(const char *text, void *ud)
{
    long v = parse_hex_color(text);
    if (v < 0) { ui_toast(tr(STR_BAD_COLOR)); return; }
    g_settings.term_ansi[(uintptr_t)ud & 15] = (uint32_t)v;
    vt_set_palette(g_settings.term_ansi);
    term_save();
}
static void ansi_row_cb(lv_event_t *e)
{
    uintptr_t i = (uintptr_t)lv_event_get_user_data(e) & 15;
    char cur[16], name[32];
    snprintf(cur, sizeof(cur), "#%06lX", (unsigned long)(g_settings.term_ansi[i] & 0xFFFFFF));
    snprintf(name, sizeof(name), "ANSI %u", (unsigned)i);
    ui_prompt(name, "#RRGGBB", cur, false, ansi_typed, (void *)i);
}

lv_obj_t *ui_ansi_create(void *arg)
{
    (void)arg;
    lv_obj_t *scr = ui_screen_base();
    ui_topbar(scr, tr(STR_TH_ANSI), true);
    lv_obj_t *c = ui_content(scr);
    static const char *names[16] = {
        "0 black", "1 red", "2 green", "3 yellow", "4 blue", "5 magenta", "6 cyan", "7 white",
        "8 bright black", "9 bright red", "10 bright green", "11 bright yellow",
        "12 bright blue", "13 bright magenta", "14 bright cyan", "15 bright white",
    };
    for (uintptr_t i = 0; i < 16; i++)
        color_row(c, names[i], g_settings.term_ansi[i], ansi_row_cb, (void *)i);
    return scr;
}
