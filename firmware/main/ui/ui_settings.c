#include "ui.h"
#include "ui_motion.h"
#include "ui_softkbd.h"
#include "settings/settings.h"
#include "sys/sysinfo.h"
#include "input/keyboard.h"
#include "ime/ime.h"
#include "servers/server_store.h"
#include "net/wifi_mgr.h"
#include "storage/sd.h"
#include "bsp/m5stack_tab5.h"
#include "esp_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void lang_cb(lv_event_t *e) { (void)e; ui_nav_push(ui_lang_create, NULL); }

static void bright_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(lv_event_get_target_obj(e));
    if (v < 5) v = 5;
    g_settings.brightness = v;
    bsp_display_brightness_set(v);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) settings_save();
}

static void rot_menu_cb(int i, void *ud)
{
    (void)ud;
    if (i < 0) return;
    g_settings.rotation = i; settings_save();
    lv_display_set_rotation(NULL, (lv_display_rotation_t)i);
    ui_nav_refresh();
}
static void rot_cb(lv_event_t *e)
{
    (void)e;
    const char *items[] = { "0°", "90°", "180°", "270°" };
    ui_menu(tr(STR_SET_ROTATION), items, 4, rot_menu_cb, NULL);
}

static const int s_offsets[] = { -720,-660,-600,-540,-480,-420,-360,-300,-240,-180,-120,-60,0,60,120,180,240,300,330,345,360,390,420,480,540,570,600,660,720,780 };
static void utc_menu_cb(int i, void *ud)
{
    (void)ud;
    if (i < 0) return;
    g_settings.utc_offset_min = s_offsets[i]; settings_save();
    sysinfo_apply_timezone();
    ui_nav_refresh();
}
static void utc_cb(lv_event_t *e)
{
    (void)e;
    static char labels[sizeof(s_offsets) / sizeof(int)][16];
    static const char *ptrs[sizeof(s_offsets) / sizeof(int)];
    for (size_t i = 0; i < sizeof(s_offsets) / sizeof(int); i++) {
        int o = s_offsets[i];
        snprintf(labels[i], 16, "UTC%c%d:%02d", o >= 0 ? '+' : '-', abs(o) / 60, abs(o) % 60);
        ptrs[i] = labels[i];
    }
    ui_menu(tr(STR_SET_UTC_OFFSET), ptrs, sizeof(s_offsets) / sizeof(int), utc_menu_cb, NULL);
}
static void ntp_cb(lv_event_t *e)
{
    (void)e;
    if (!wifi_mgr_is_connected()) { ui_toast(tr(STR_WIFI_NOT_CONNECTED)); return; }
    sysinfo_start_sntp();
    ui_toast(tr(STR_PLEASE_WAIT));
}

static void ime_menu_cb(int i, void *ud) { (void)ud; if (i >= 0) { ime_set_source((ime_src_t)i); ui_nav_refresh(); } }
static void ime_cb(lv_event_t *e)
{
    (void)e;
    const char *items[] = { tr(STR_IME_EN), tr(STR_IME_JA), tr(STR_IME_ZH) };
    ui_menu(tr(STR_IME_MODE), items, 3, ime_menu_cb, NULL);
}
static void kana_menu_cb(int i, void *ud) { (void)ud; if (i >= 0) { ime_set_ja_mode((ime_ja_mode_t)i); ui_nav_refresh(); } }
static void kana_cb(lv_event_t *e)
{
    (void)e;
    const char *items[] = { tr(STR_IME_HIRA), tr(STR_IME_KATA) };
    ui_menu(tr(STR_IME_JA), items, 2, kana_menu_cb, NULL);
}
static void skk_sd_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    g_settings.ime_skk_sd = on; settings_save();
    if (on) {
        lv_obj_t *b = ui_busy(tr(STR_PLEASE_WAIT));
        lv_refr_now(NULL);
        bool ok = ime_load_sd_dict(SD_MOUNT "/skk/SKK-JISYO.L");
        ui_busy_close(b);
        ui_toast(ok ? tr(STR_IME_DICT_LOADED) : tr(STR_FAILED));
    }
}

static void kbd_layout_menu_cb(int i, void *ud)
{
    (void)ud;
    if (i < 0 || i >= KBD_LAYOUT_COUNT || i == g_settings.kbd_layout) return;
    g_settings.kbd_layout = i;
    settings_save();
    keyboard_set_layout(i);
    ui_nav_refresh();
}
static void kbd_layout_cb(lv_event_t *e)
{
    (void)e;
    const char *items[KBD_LAYOUT_COUNT] = { tr(STR_KBD_LAYOUT_STOCK), tr(STR_KBD_LAYOUT_JIS) };
    ui_menu(tr(STR_SET_KBD_LAYOUT), items, KBD_LAYOUT_COUNT, kbd_layout_menu_cb, NULL);
}

static void led_bright_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(lv_event_get_target_obj(e));
    g_settings.kbd_led_bright = v;
    keyboard_set_led(g_settings.kbd_led_mode, v, g_settings.kbd_led_rgb[0], g_settings.kbd_led_rgb[1], g_settings.kbd_led_rgb[2]);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) settings_save();
}
static void led_mode_cb(lv_event_t *e)
{
    g_settings.kbd_led_mode = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    settings_save();
    keyboard_set_led(g_settings.kbd_led_mode, g_settings.kbd_led_bright, g_settings.kbd_led_rgb[0], g_settings.kbd_led_rgb[1], g_settings.kbd_led_rgb[2]);
}
static const uint32_t s_led_presets[] = { 0x0080FF, 0xFFFFFF, 0xFF4040, 0x40FF40, 0xFFD700, 0xFF40FF, 0x00FFFF, 0xFF8000 };
static void led_color_menu_cb(int i, void *ud)
{
    (void)ud; if (i < 0) return;
    uint32_t c = s_led_presets[i];
    g_settings.kbd_led_rgb[0] = c >> 16; g_settings.kbd_led_rgb[1] = (c >> 8) & 255; g_settings.kbd_led_rgb[2] = c & 255;
    g_settings.kbd_led_mode = 1;
    keyboard_set_led(1, g_settings.kbd_led_bright, g_settings.kbd_led_rgb[0], g_settings.kbd_led_rgb[1], g_settings.kbd_led_rgb[2]);
    settings_save(); ui_nav_refresh();
}
static void led_color_cb(lv_event_t *e)
{
    (void)e;
    const char *items[] = { "Blue", "White", "Red", "Green", "Yellow", "Magenta", "Cyan", "Orange" };
    ui_menu(tr(STR_SET_KBD_LED), items, 8, led_color_menu_cb, NULL);
}
static void ss_cb(lv_event_t *e) { (void)e; ui_nav_push(ui_screensaver_settings_create, NULL); }
static void appearance_cb(lv_event_t *e) { (void)e; ui_nav_push(ui_appearance_create, NULL); }
static void backup_cb(lv_event_t *e) { (void)e; ui_nav_push(ui_backup_create, NULL); }

static void creds_sd_cb(lv_event_t *e)
{
    bool want = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    if (!server_store_set_on_sd(want)) ui_toast(tr(STR_CREDS_NO_SD));
    ui_nav_refresh();
}
static void creds_reload_cb(lv_event_t *e)
{
    (void)e;
    server_store_reload();
    ui_toastf("%s: %d", tr(STR_SERVERS), server_store_count());
}
static void reboot_yes(bool yes, void *ud) { (void)ud; if (yes) esp_restart(); }
static void reboot_cb(lv_event_t *e) { (void)e; ui_confirm(tr(STR_REBOOT), tr(STR_REBOOT_Q), reboot_yes, NULL); }
static void about_cb(lv_event_t *e) { (void)e; char b[400]; snprintf(b, sizeof(b), "v%s\n%s", sysinfo_app_version(), tr(STR_ABOUT_TEXT)); ui_alert(tr(STR_ABOUT), b); }
static void ota_url_cb(const char *v, void *ud) { (void)ud; if (v) { strlcpy(g_settings.ota_url, v, sizeof(g_settings.ota_url)); settings_save(); ui_nav_refresh(); } }
static void ota_url_row_cb(lv_event_t *e) { (void)e; ui_prompt(tr(STR_OTA_URL), "https://", g_settings.ota_url, false, ota_url_cb, NULL); }

static lv_obj_t *slider_row(lv_obj_t *parent, const char *label, int min, int max, int val, lv_event_cb_t cb)
{
    lv_obj_t *card = ui_card(parent);
    ui_label(card, label, FONT_BODY, UI_TEXT);
    lv_obj_t *s = lv_slider_create(card);
    lv_obj_set_width(s, LV_PCT(96));
    lv_obj_set_style_margin_all(s, 8, 0);
    lv_slider_set_range(s, min, max);
    lv_slider_set_value(s, val, LV_ANIM_OFF);
    lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s, cb, LV_EVENT_RELEASED, NULL);
    return s;
}

static void sb_bit_cb(lv_event_t *e)
{
    unsigned bit = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    if (lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED)) g_settings.status_items |= bit;
    else g_settings.status_items &= ~bit;
    settings_save();
}

/* Keep the selected category when returning from a nested setting. Each panel
 * has its own scroll area beside the category menu. */
static unsigned s_category;
static const str_id_t s_categories[] = {
    STR_SET_SYSTEM, STR_SET_DISPLAY, STR_SET_STATUSBAR, STR_SET_INPUT,
    STR_SET_SECURITY, STR_SET_BACKUP, STR_ABOUT
};
static void category_cb(lv_event_t *e)
{
    unsigned next = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    if (next >= sizeof(s_categories) / sizeof(s_categories[0]) || next == s_category) return;
    lv_obj_t *nav = lv_obj_get_parent(lv_event_get_target_obj(e));
    lv_obj_t *deck = lv_obj_get_child(lv_obj_get_parent(nav), 1);
    unsigned previous = s_category;
    lv_obj_add_flag(lv_obj_get_child(deck, previous), LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(lv_obj_get_child(deck, next), LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *old_button = lv_obj_get_child(nav, previous);
    lv_obj_t *new_button = lv_obj_get_child(nav, next);
    lv_obj_set_style_bg_color(old_button, UI_CARD_HI, 0);
    lv_obj_set_style_text_color(old_button, UI_TEXT, 0);
    lv_obj_set_style_bg_color(new_button, UI_ACCENT, 0);
    lv_obj_set_style_text_color(new_button, ui_theme_pixel() ? UI_BG : UI_TEXT, 0);
    s_category = next;
    ui_motion_start(lv_obj_get_screen(deck), deck, next < previous);
}
static lv_obj_t *settings_panel(lv_obj_t *deck, unsigned category)
{
    lv_obj_t *panel = ui_card(deck);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    if (category != s_category) lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    ui_section_title(panel, tr(s_categories[category]));
    return panel;
}

lv_obj_t *ui_settings_create(void *arg)
{
    (void)arg;
    lv_obj_t *scr = ui_screen_base();
    ui_topbar(scr, tr(STR_SETTINGS), true);
    lv_obj_t *body = ui_content(scr);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, ui_theme_pad(14), 0);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *nav = ui_card(body);
    lv_obj_set_width(nav, LV_PCT(24));
    lv_obj_set_style_min_width(nav, 212, 0);
    lv_obj_set_height(nav, LV_PCT(100));
    lv_obj_add_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(nav, LV_DIR_VER);
    for (unsigned i = 0; i < sizeof(s_categories) / sizeof(s_categories[0]); i++) {
        lv_obj_t *b = ui_button_colored(nav, tr(s_categories[i]),
                                       i == s_category ? UI_ACCENT : UI_CARD_HI,
                                       category_cb, (void *)(uintptr_t)i);
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_set_height(b, 64);
        lv_obj_t *label = lv_obj_get_child(b, 0);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_font(label, FONT_SMALL, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    }
    lv_obj_t *deck = lv_obj_create(body);
    lv_obj_remove_style_all(deck);
    lv_obj_set_height(deck, LV_PCT(100));
    lv_obj_set_flex_grow(deck, 1);
    lv_obj_remove_flag(deck, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *c;
    char buf[128];

    c = settings_panel(deck, 0);
    ui_setting_row(c, tr(STR_SET_LANGUAGE), i18n_lang_name(i18n_get_lang()), lang_cb, NULL);
    int o = g_settings.utc_offset_min;
    snprintf(buf, sizeof(buf), "UTC%c%d:%02d", o >= 0 ? '+' : '-', abs(o) / 60, abs(o) % 60);
    ui_setting_row(c, tr(STR_SET_UTC_OFFSET), buf, utc_cb, NULL);
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    ui_setting_row(c, tr(STR_SET_NTP), buf, ntp_cb, NULL);
    ui_label(c, tr(STR_SET_UTC_NOTE), FONT_SMALL, UI_MUTED);
    ui_setting_row(c, tr(STR_OTA_URL), g_settings.ota_url, ota_url_row_cb, NULL);

    c = settings_panel(deck, 1);
    slider_row(c, tr(STR_SET_BRIGHTNESS), 5, 100, g_settings.brightness, bright_cb);
    snprintf(buf, sizeof(buf), "%d°", g_settings.rotation * 90);
    ui_setting_row(c, tr(STR_SET_ROTATION), buf, rot_cb, NULL);
    ui_setting_row(c, tr(STR_SET_THEME), "", appearance_cb, NULL);
    ui_setting_row(c, tr(STR_SET_SCREENSAVER), g_settings.ss_enable ? tr(STR_ON) : tr(STR_OFF), ss_cb, NULL);
    ui_label(c, tr(STR_SET_SCREENSHOT_NOTE), FONT_SMALL, UI_MUTED);

    c = settings_panel(deck, 2);
    static const struct { str_id_t id; unsigned bit; } sb_items[] = {
        { STR_SB_WIFI, STATUS_WIFI }, { STR_SB_TAILSCALE, STATUS_TAILSCALE },
        { STR_SB_IP, STATUS_IP },     { STR_SB_BATTERY, STATUS_BATTERY },
        { STR_SB_KEYBOARD, STATUS_KEYBOARD }, { STR_SB_CLOCK, STATUS_CLOCK },
        { STR_SB_DATE, STATUS_DATE }, { STR_SB_YEAR, STATUS_YEAR },
    };
    for (size_t i = 0; i < sizeof(sb_items) / sizeof(sb_items[0]); i++)
        ui_switch_row(c, tr(sb_items[i].id), (g_settings.status_items & sb_items[i].bit) != 0,
                      sb_bit_cb, (void *)(uintptr_t)sb_items[i].bit);
    ui_label(c, tr(STR_SB_NOTE), FONT_SMALL, UI_MUTED);

    c = settings_panel(deck, 3);
    const char *src = ime_get_source() == IME_SRC_JA ? tr(STR_IME_JA) : ime_get_source() == IME_SRC_ZH ? tr(STR_IME_ZH) : tr(STR_IME_EN);
    ui_setting_row(c, tr(STR_IME_MODE), src, ime_cb, NULL);
    ui_setting_row(c, tr(STR_IME_JA), ime_get_ja_mode() == IME_JA_KATA ? tr(STR_IME_KATA) : tr(STR_IME_HIRA), kana_cb, NULL);
    ui_label(c, tr(STR_IME_SWITCH_KEY), FONT_SMALL, UI_MUTED);
    ui_label(c, tr(STR_GESTURE_NOTE), FONT_SMALL, UI_MUTED);
    snprintf(buf, sizeof(buf), "%s: %s", tr(STR_IME_SKK_DICT), ime_dict_info());
    ui_switch_row(c, tr(STR_IME_DICT_SD), g_settings.ime_skk_sd, skk_sd_cb, NULL);
    ui_label(c, buf, FONT_SMALL, UI_MUTED);
    snprintf(buf, sizeof(buf), tr(STR_SET_KBD_STATUS), keyboard_present() ? tr(STR_PRESENT) : (keyboard_usb_present() ? "USB" : tr(STR_ABSENT)));
    ui_label(c, buf, FONT_SMALL, UI_MUTED);
    ui_setting_row(c, tr(STR_SET_KBD_LAYOUT),
                   tr(g_settings.kbd_layout == KBD_LAYOUT_JIS ? STR_KBD_LAYOUT_JIS : STR_KBD_LAYOUT_STOCK),
                   kbd_layout_cb, NULL);
    if (g_settings.kbd_layout != KBD_LAYOUT_STOCK) ui_label(c, tr(STR_SET_KBD_LAYOUT_NOTE), FONT_SMALL, UI_MUTED);
    ui_switch_row(c, tr(STR_SET_KBD_LED), g_settings.kbd_led_mode, led_mode_cb, NULL);
    slider_row(c, tr(STR_SET_KBD_LED_BRIGHT), 0, 100, g_settings.kbd_led_bright, led_bright_cb);
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", g_settings.kbd_led_rgb[0], g_settings.kbd_led_rgb[1], g_settings.kbd_led_rgb[2]);
    lv_obj_t *lc = ui_setting_row(c, tr(STR_SET_KBD_LED), buf, led_color_cb, NULL);
    lv_obj_set_style_text_color(lv_obj_get_user_data(lc), UI_TEXT, 0);

    c = settings_panel(deck, 4);
    ui_switch_row(c, tr(STR_CREDS_ON_SD), g_settings.creds_on_sd, creds_sd_cb, NULL);
    ui_label(c, tr(STR_CREDS_NOTE), FONT_SMALL, UI_MUTED);
    if (g_settings.creds_on_sd) ui_setting_row(c, tr(STR_CREDS_RELOAD), "", creds_reload_cb, NULL);

    c = settings_panel(deck, 5);
    ui_setting_row(c, tr(STR_SET_BACKUP), "", backup_cb, NULL);

    c = settings_panel(deck, 6);
    snprintf(buf, sizeof(buf), "v%s", sysinfo_app_version());
    ui_setting_row(c, tr(STR_ABOUT), buf, about_cb, NULL);
    ui_button_colored(c, tr(STR_REBOOT), UI_DANGER, reboot_cb, NULL);
    return scr;
}
