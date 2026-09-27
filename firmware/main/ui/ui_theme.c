#include "ui.h"
#include "ui_theme.h"
#include "settings/settings.h"
#include <string.h>

/* A preset is a whole look, not just a palette: corner rounding, padding
 * density, whether the home tiles take the accent colour, the glow behind
 * cards, and the terminal's own colours all come with it. */
typedef struct {
    const char *name;
    uint32_t    c[UI_C_COUNT];
    uint8_t     radius, density, tile_accent, glow, scanlines;
    uint32_t    term_fg, term_bg, term_cursor;
    uint32_t    term_ansi[16];
} preset_t;

/* The VS Code palette, used by every preset that does not want a tinted one. */
#define ANSI_STANDARD \
    { 0x000000, 0xCD3131, 0x0DBC79, 0xE5E510, 0x2472C8, 0xBC3FBC, 0x11A8CD, 0xE5E5E5, \
      0x666666, 0xF14C4C, 0x23D18B, 0xF5F543, 0x3B8EEA, 0xD670D6, 0x29B8DB, 0xFFFFFF }

/* BG, CARD, CARD_HI, ACCENT, ACCENT2, DANGER, TEXT, MUTED */
static const preset_t s_presets[] = {
    { "Orange EL", { 0x000000, 0x190B00, 0x331A00, 0xFF8C00, 0xFFB84D, 0xFF3B15, 0xFFA836, 0x9A5F14 },
      0, UI_DENSITY_NORMAL, 1, 1, 1, 0xFF9800, 0x241200, 0xFFD180,
      { 0x383838, 0xD77A56, 0xA3B86C, 0xE8C928, 0xA8C7FA, 0xB8B8EE, 0xA8D8EA, 0xB5AFA5,
        0x888176, 0xF08D6B, 0xC1D58A, 0xFFCC00, 0xC2D9FF, 0xC8C8FF, 0xC2ECF5, 0xFFFFFF } },

    { "Retro CRT", { 0x001208, 0x042213, 0x0A3A22, 0x33FF77, 0xAAFF33, 0xFF5544, 0x66FFAA, 0x2E9E63 },
      3, UI_DENSITY_NORMAL, 1, 1, 1, 0x4CFF8A, 0x000A05, 0xAFFFCB,
      { 0x00170D, 0xFF6B5B, 0x33FF77, 0xC8FF5A, 0x3FD9A0, 0x8CFFC2, 0x5AFFD0, 0x9BFFC4,
        0x1F6B45, 0xFF9A8C, 0x86FFAF, 0xE4FF9C, 0x7FF0C6, 0xC2FFE0, 0x9EFFE6, 0xE8FFF2 } },

    { "Pixel",  { 0x0B0B14, 0x1C1C2E, 0x32325A, 0x00E5FF, 0x9EFF3D, 0xFF4757, 0xF2F2F7, 0x8A8AA8 },
      0, UI_DENSITY_COMPACT, 0, 0, 0, 0xF2F2F7, 0x0B0B14, 0x00E5FF,
      { 0x0B0B14, 0xFF4757, 0x9EFF3D, 0xFFE347, 0x4D7BFF, 0xD65DFF, 0x00E5FF, 0xC8C8DC,
        0x494966, 0xFF7A85, 0xC4FF7A, 0xFFF08A, 0x85A8FF, 0xE79BFF, 0x7AF0FF, 0xFFFFFF } },

    { "Dark",   { 0x0F172A, 0x1E293B, 0x334155, 0x2F6FED, 0x22C55E, 0xEF4444, 0xE5E7EB, 0x94A3B8 },
      12, UI_DENSITY_NORMAL, 0, 0, 0, 0xE5E7EB, 0x000000, 0xE5E7EB, ANSI_STANDARD },

    { "Light",  { 0xF3F4F6, 0xFFFFFF, 0xE5E7EB, 0x2F6FED, 0x16A34A, 0xDC2626, 0x111827, 0x6B7280 },
      12, UI_DENSITY_NORMAL, 0, 0, 0, 0x1F2430, 0xFFFFFF, 0x2F6FED,
      { 0x3B4252, 0xC0392B, 0x1E8449, 0xB7950B, 0x1F618D, 0x884EA0, 0x117A65, 0x2E3440,
        0x6B7280, 0xE74C3C, 0x28B463, 0xD4AC0D, 0x2E86C1, 0xA569BD, 0x17A589, 0x111827 } },

    { "Nord",   { 0x2E3440, 0x3B4252, 0x4C566A, 0x88C0D0, 0xA3BE8C, 0xBF616A, 0xECEFF4, 0x8FBCBB },
      10, UI_DENSITY_NORMAL, 0, 0, 0, 0xECEFF4, 0x2E3440, 0x88C0D0, ANSI_STANDARD },

    { "Solarized Dark", { 0x002B36, 0x073642, 0x0E4B5A, 0x268BD2, 0x859900, 0xDC322F, 0xEEE8D5, 0x93A1A1 },
      10, UI_DENSITY_NORMAL, 0, 0, 0, 0xEEE8D5, 0x002B36, 0x268BD2, ANSI_STANDARD },

    { "Gruvbox", { 0x282828, 0x3C3836, 0x504945, 0x83A598, 0xB8BB26, 0xFB4934, 0xEBDBB2, 0xA89984 },
      10, UI_DENSITY_NORMAL, 0, 0, 0, 0xEBDBB2, 0x282828, 0xB8BB26, ANSI_STANDARD },
    /* Append presets: persisted indexes of existing themes must stay stable. */
    { "Classic",  { 0x080F17, 0x112130, 0x263C4E, 0x367EF2, 0x37CE78, 0xEF4444, 0xEDF3F8, 0x9CADBF },
      12, UI_DENSITY_NORMAL, 0, 0, 0, 0xE5E7EB, 0x000000, 0xE5E7EB, ANSI_STANDARD },
};
#define PRESET_N ((int)(sizeof(s_presets) / sizeof(s_presets[0])))

static uint32_t s_col[UI_C_COUNT];
static int      s_radius = UI_RADIUS_DESIGN;
static int      s_density = UI_DENSITY_NORMAL;
static bool     s_tile_accent;
static bool     s_glow, s_scanlines;

void ui_theme_init(void)
{
    uint8_t p = g_settings.theme_preset;
    for (int i = 0; i < UI_C_COUNT; i++)
        s_col[i] = (p < PRESET_N) ? s_presets[p].c[i] : (g_settings.theme_colors[i] & 0xFFFFFF);
    s_radius = g_settings.theme_radius;
    if (s_radius > 24) s_radius = 24;
    s_density = g_settings.theme_density <= UI_DENSITY_ROOMY ? g_settings.theme_density : UI_DENSITY_NORMAL;
    s_tile_accent = g_settings.theme_tile_accent != 0;
    s_glow = g_settings.theme_glow != 0;
    s_scanlines = g_settings.theme_scanlines != 0;
}

lv_color_t ui_theme_color(ui_color_id_t id) { return lv_color_hex(s_col[id < UI_C_COUNT ? id : 0]); }
uint32_t   ui_theme_color_hex(ui_color_id_t id) { return s_col[id < UI_C_COUNT ? id : 0]; }

void ui_theme_set_color(ui_color_id_t id, uint32_t rgb)
{
    if (id >= UI_C_COUNT) return;
    /* Editing a colour turns the preset into a custom theme, keeping the rest. */
    if (g_settings.theme_preset < PRESET_N) {
        for (int i = 0; i < UI_C_COUNT; i++) g_settings.theme_colors[i] = s_col[i];
        g_settings.theme_preset = UI_THEME_CUSTOM;
    }
    g_settings.theme_colors[id] = rgb & 0xFFFFFF;
    s_col[id] = rgb & 0xFFFFFF;
}

int ui_theme_radius(int design)
{
    int r = design * s_radius / UI_RADIUS_DESIGN;
    return r < 0 ? 0 : r;
}

int ui_theme_pad(int design)
{
    switch (s_density) {
    case UI_DENSITY_COMPACT: return design * 2 / 3;
    case UI_DENSITY_ROOMY:   return design * 4 / 3;
    default:                 return design;
    }
}

bool        ui_theme_pixel(void) { return s_radius == 0 || s_scanlines; }

bool        ui_theme_tile_accent(void) { return s_tile_accent; }
bool        ui_theme_glow(void) { return s_glow; }
bool        ui_theme_scanlines(void) { return s_scanlines; }

/* A luminous edge instead of a blurred shadow: fixed-width outlines draw
 * without allocating/rasterizing a shadow mask on every invalidated card. */
void ui_theme_glow_apply(lv_obj_t *obj)
{
    if (!s_glow) return;
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 2, 0);
    lv_obj_set_style_outline_pad(obj, 1, 0);
    lv_obj_set_style_outline_color(obj, ui_theme_color(UI_C_ACCENT), 0);
    lv_obj_set_style_outline_opa(obj, LV_OPA_20, 0);
}
int         ui_theme_preset_count(void) { return PRESET_N; }
const char *ui_theme_preset_name(int i) { return (i >= 0 && i < PRESET_N) ? s_presets[i].name : "Custom"; }

/* Apply the requested CLI reference palette without changing the system UI. */
void ui_theme_set_terminal_amber(void)
{
    const preset_t *p = &s_presets[0];
    g_settings.term_fg = p->term_fg;
    g_settings.term_bg = p->term_bg;
    g_settings.term_cursor = p->term_cursor;
    memcpy(g_settings.term_ansi, p->term_ansi, sizeof(p->term_ansi));
}

void ui_theme_set_preset(int i)
{
    if (i < 0 || i >= PRESET_N) return;
    const preset_t *p = &s_presets[i];
    g_settings.theme_preset = (uint8_t)i;
    for (int k = 0; k < UI_C_COUNT; k++) g_settings.theme_colors[k] = p->c[k];
    g_settings.theme_radius = p->radius;
    g_settings.theme_density = p->density;
    g_settings.theme_tile_accent = p->tile_accent;
    g_settings.theme_glow = p->glow;
    g_settings.theme_scanlines = p->scanlines;
    g_settings.term_fg = p->term_fg;
    g_settings.term_bg = p->term_bg;
    g_settings.term_cursor = p->term_cursor;
    memcpy(g_settings.term_ansi, p->term_ansi, sizeof(p->term_ansi));
    ui_theme_init();
}

const char *ui_theme_color_name(ui_color_id_t id)
{
    switch (id) {
    case UI_C_BG:      return tr(STR_TH_BG);
    case UI_C_CARD:    return tr(STR_TH_CARD);
    case UI_C_CARD_HI: return tr(STR_TH_CARD_HI);
    case UI_C_ACCENT:  return tr(STR_TH_ACCENT);
    case UI_C_ACCENT2: return tr(STR_TH_ACCENT2);
    case UI_C_DANGER:  return tr(STR_TH_DANGER);
    case UI_C_TEXT:    return tr(STR_TH_TEXT);
    default:           return tr(STR_TH_MUTED);
    }
}

void ui_theme_apply(void)
{
    ui_theme_init();
    settings_save();
    ui_theme_reinit_lvgl();
    ui_nav_refresh();
}

/* Shared styles sit above LVGL's base theme and below screen-local styles.
 * This includes controls created directly by screens (sliders, switches,
 * checkboxes, keyboard keys), not just the ui_* convenience constructors. */
static lv_style_t s_surface;
static lv_style_t s_control, s_pressed, s_focus, s_disabled;
static lv_style_t s_track, s_indicator, s_knob, s_scrollbar;
static lv_theme_t *s_widget_theme;
static lv_style_transition_dsc_t s_phosphor_release, s_phosphor_press;
static const lv_style_prop_t s_phosphor_props[] = { LV_STYLE_BG_COLOR, LV_STYLE_BORDER_COLOR, 0 };

static void widget_apply(lv_theme_t *theme, lv_obj_t *obj)
{
    (void)theme;
    lv_obj_add_style(obj, &s_scrollbar, LV_PART_SCROLLBAR);
    if (lv_obj_check_type(obj, &lv_obj_class)) lv_obj_add_style(obj, &s_surface, LV_PART_MAIN);
    if (lv_obj_check_type(obj, &lv_button_class) || lv_obj_check_type(obj, &lv_textarea_class)) {
        lv_obj_add_style(obj, &s_control, LV_PART_MAIN);
        lv_obj_add_style(obj, &s_pressed, LV_STATE_PRESSED);
        lv_obj_add_style(obj, &s_focus, LV_STATE_FOCUSED);
        lv_obj_add_style(obj, &s_disabled, LV_STATE_DISABLED);
    }
    if (lv_obj_check_type(obj, &lv_slider_class) || lv_obj_check_type(obj, &lv_bar_class) ||
        lv_obj_check_type(obj, &lv_switch_class)) {
        lv_obj_add_style(obj, &s_track, LV_PART_MAIN);
        lv_obj_add_style(obj, &s_indicator, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &s_knob, LV_PART_KNOB);
        lv_obj_add_style(obj, &s_disabled, LV_STATE_DISABLED);
        lv_obj_add_style(obj, &s_focus, LV_STATE_FOCUSED);
        if (lv_obj_check_type(obj, &lv_switch_class)) {
            lv_obj_add_style(obj, &s_track, LV_PART_INDICATOR);
            lv_obj_add_style(obj, &s_indicator, LV_PART_INDICATOR | LV_STATE_CHECKED);
        }
    }
    if (lv_obj_check_type(obj, &lv_checkbox_class)) {
        lv_obj_add_style(obj, &s_control, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &s_indicator, LV_PART_INDICATOR | LV_STATE_CHECKED);
    }
}

void ui_theme_install_widgets(lv_theme_t *base)
{
    static bool initialized;
    lv_style_t *styles[] = { &s_surface, &s_control, &s_pressed, &s_focus, &s_disabled,
                            &s_track, &s_indicator, &s_knob, &s_scrollbar };
    for (unsigned i = 0; i < sizeof(styles) / sizeof(styles[0]); i++) {
        if (initialized) lv_style_reset(styles[i]);
        else lv_style_init(styles[i]);
    }
    initialized = true;
    lv_style_set_text_color(&s_surface, UI_TEXT);
    lv_style_set_radius(&s_control, ui_theme_radius(10));
    lv_style_set_bg_color(&s_control, UI_CARD);
    lv_style_set_text_color(&s_control, UI_TEXT);
    lv_style_set_border_color(&s_control, ui_theme_pixel() ? UI_MUTED : UI_CARD_HI);
    lv_style_set_border_width(&s_control, 1);
    lv_style_set_shadow_width(&s_control, 0);
    if (ui_theme_pixel()) {
        /* Quick excitation, slower decay, confined to the touched control. */
        lv_style_transition_dsc_init(&s_phosphor_press, s_phosphor_props, lv_anim_path_ease_out, 40, 0, NULL);
        lv_style_transition_dsc_init(&s_phosphor_release, s_phosphor_props, lv_anim_path_ease_out, 120, 0, NULL);
        lv_style_set_transition(&s_control, &s_phosphor_release);
        lv_style_set_transition(&s_pressed, &s_phosphor_press);
        lv_style_set_transform_width(&s_pressed, 0);
        lv_style_set_transform_height(&s_pressed, 0);
    }
    lv_style_set_bg_color(&s_pressed, UI_CARD_HI);
    lv_style_set_border_color(&s_pressed, UI_ACCENT);
    lv_style_set_outline_color(&s_focus, UI_ACCENT);
    lv_style_set_outline_width(&s_focus, 2);
    lv_style_set_outline_pad(&s_focus, 2);
    lv_style_set_opa(&s_disabled, LV_OPA_40);
    lv_style_set_bg_color(&s_track, UI_CARD_HI);
    lv_style_set_bg_opa(&s_track, LV_OPA_COVER);
    lv_style_set_radius(&s_track, ui_theme_pixel() ? 0 : LV_RADIUS_CIRCLE);
    lv_style_set_bg_color(&s_indicator, UI_ACCENT);
    lv_style_set_bg_opa(&s_indicator, LV_OPA_COVER);
    lv_style_set_text_color(&s_indicator, UI_BG);
    lv_style_set_radius(&s_indicator, ui_theme_pixel() ? 0 : LV_RADIUS_CIRCLE);
    lv_style_set_bg_color(&s_knob, UI_TEXT);
    lv_style_set_radius(&s_knob, ui_theme_pixel() ? 0 : LV_RADIUS_CIRCLE);
    lv_style_set_shadow_width(&s_knob, 0);
    lv_style_set_bg_color(&s_scrollbar, UI_MUTED);
    lv_style_set_radius(&s_scrollbar, ui_theme_pixel() ? 0 : LV_RADIUS_CIRCLE);
    if (!s_widget_theme) s_widget_theme = lv_theme_create();
    if (!s_widget_theme) { lv_display_set_theme(NULL, base); return; }
    lv_theme_copy(s_widget_theme, base);
    lv_theme_set_parent(s_widget_theme, base);
    lv_theme_set_apply_cb(s_widget_theme, widget_apply);
    lv_display_set_theme(NULL, s_widget_theme);
    for (unsigned i = 0; i < sizeof(styles) / sizeof(styles[0]); i++)
        lv_obj_report_style_change(styles[i]);
}
