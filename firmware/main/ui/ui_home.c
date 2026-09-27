#include "ui.h"
#include "settings/settings.h"
#include "sys/sysinfo.h"
#include "net/wifi_mgr.h"
#include "storage/sd.h"
#include "input/keyboard.h"
#include "media/audio_player.h"
#include "net/tailscale.h"
#include <stdio.h>
#include <time.h>

typedef struct { str_id_t title; const char *icon; ui_screen_create_fn create; lv_color_t color;
                 void (*quick)(void); } tile_t;   /* quick: long-press shortcut */

static void tile_cb(lv_event_t *e)
{
    ui_screen_create_fn fn = lv_event_get_user_data(e);
    ui_nav_push(fn, NULL);
}

/* Long-pressing a tile runs its most useful shortcut instead of opening it. */
static void quick_new_server(void) { ui_nav_push(ui_server_form_create, (void *)(intptr_t)-1); }
static void quick_usb_files(void)  { ui_nav_push(ui_files_create, (void *)USB_MOUNT); }
static void quick_music(void)
{
    audio_player_toggle();
    ui_toast(audio_player_state() == AP_PLAYING ? tr(STR_PLAY) : tr(STR_PAUSE));
}
static void quick_wifi(void)
{
    if (wifi_mgr_is_connected()) { wifi_mgr_disconnect(); ui_toast(tr(STR_DISCONNECTED)); }
    else ui_nav_push(ui_wifi_create, NULL);
}
static void quick_tailscale(void)
{
    g_settings.ts_enable = !g_settings.ts_enable;
    settings_save();
    if (g_settings.ts_enable) { tailscale_start(); ui_toast(tr(STR_ON)); }
    else { tailscale_stop(); ui_toast(tr(STR_OFF)); }
}
static void quick_appearance(void) { ui_nav_push(ui_appearance_create, NULL); }

static void tile_long_cb(lv_event_t *e)
{
    void (*fn)(void) = lv_event_get_user_data(e);
    if (fn) fn();
}

static void title_click_cb(lv_event_t *e) { (void)e; ui_screensaver_start(); }

lv_obj_t *ui_home_create(void *arg)
{
    (void)arg;
    lv_obj_t *scr = ui_screen_base();
    lv_obj_t *bar = ui_topbar(scr, tr(STR_APP_TITLE), false);
    lv_obj_t *title = lv_obj_get_child(bar, 0);
    if (lv_display_get_horizontal_resolution(NULL) < 1000)
        lv_obj_set_style_text_font(title, FONT_BODY, 0);
    lv_obj_add_flag(title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(title, title_click_cb, LV_EVENT_CLICKED, NULL);
    ui_statusbar_attach(bar);

    lv_obj_t *c = ui_content(scr);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(c, ui_theme_pad(14), 0);
    lv_obj_set_style_pad_row(c, ui_theme_pad(14), 0);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    const tile_t tiles[] = {
        { STR_HOME_SSH,      LV_SYMBOL_KEYBOARD, ui_servers_create,  lv_color_hex(0x2F6FED), quick_new_server },
        { STR_HOME_FILES,    LV_SYMBOL_DIRECTORY, ui_files_create,   lv_color_hex(0xF59E0B), quick_usb_files },
        { STR_HOME_MUSIC,    LV_SYMBOL_AUDIO,    ui_music_create,    lv_color_hex(0x0EA5A4), quick_music },
        { STR_HOME_CAMERA,   LV_SYMBOL_IMAGE,    ui_camera_create,   lv_color_hex(0xD97706) },
        { STR_HOME_WIFI,     LV_SYMBOL_WIFI,     ui_wifi_create,     lv_color_hex(0x8B5CF6), quick_wifi },
        { STR_TAILSCALE,     NULL,               ui_tailscale_create, lv_color_hex(0x6366F1), quick_tailscale },   /* NULL: drawn mark */
        { STR_HOME_WEBFM,    LV_SYMBOL_UPLOAD,   ui_webfm_create,    lv_color_hex(0x10B981) },
        { STR_HOME_USB,      LV_SYMBOL_USB,      ui_usb_create,      lv_color_hex(0x64748B) },
        { STR_HOME_OTA,      LV_SYMBOL_DOWNLOAD, ui_ota_create,      lv_color_hex(0xEC4899) },
        { STR_HOME_DEVINFO,  LV_SYMBOL_LIST,     ui_devinfo_create,  lv_color_hex(0x0284C7) },
        { STR_HOME_SETTINGS, LV_SYMBOL_SETTINGS, ui_settings_create, lv_color_hex(0x475569), quick_appearance },
    };
    int32_t w = lv_display_get_horizontal_resolution(NULL);
    int cols = w > 1000 ? 6 : 3;
    int pad = ui_theme_pad(14);
    int32_t tile_w = (w - 2 * pad - pad * (cols - 1)) / cols;
    int rows = (sizeof(tiles) / sizeof(tiles[0]) + cols - 1) / cols;
    /* Reserve the bottom strip for the persistent playback controls. */
    int32_t tile_h = (lv_display_get_vertical_resolution(NULL) - UI_TOPBAR_H - 56 -
                      2 * pad - pad * (rows - 1)) / rows;
    if (tile_h < 150) tile_h = 150;
    for (size_t i = 0; i < sizeof(tiles) / sizeof(tiles[0]); i++) {
        lv_obj_t *b = lv_button_create(c);
        lv_obj_set_size(b, tile_w, tile_h);
        lv_obj_set_style_bg_color(b, UI_CARD, 0);
        lv_obj_set_style_bg_color(b, UI_CARD_HI, LV_STATE_PRESSED);
        lv_obj_set_style_radius(b, ui_theme_radius(18), 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, UI_MUTED, 0);
        lv_obj_set_style_border_color(b, UI_ACCENT, LV_STATE_PRESSED);
        ui_theme_glow_apply(b);
        lv_obj_set_style_pad_all(b, ui_theme_pad(14), 0);
        lv_obj_add_event_cb(b, tile_cb, LV_EVENT_SHORT_CLICKED, tiles[i].create);
        if (tiles[i].quick) lv_obj_add_event_cb(b, tile_long_cb, LV_EVENT_LONG_PRESSED, tiles[i].quick);
        lv_color_t icol = ui_theme_tile_accent() ? UI_ACCENT : tiles[i].color;
        lv_obj_t *icon;
        if (tiles[i].icon) {
            icon = lv_label_create(b);
            lv_label_set_text(icon, tiles[i].icon);
            lv_obj_set_style_text_font(icon, FONT_ICON, 0);
            lv_obj_set_style_text_color(icon, icol, 0);
        } else {
            icon = ui_tailscale_icon(b, 7, 4);
            ui_tailscale_icon_set_color(icon, icol, LV_OPA_COVER);
        }
        lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);
        char number[8]; snprintf(number, sizeof(number), "%02u", (unsigned)i + 1);
        lv_obj_t *index = lv_label_create(b);
        lv_label_set_text(index, number);
        lv_obj_set_style_text_font(index, FONT_SMALL, 0);
        lv_obj_set_style_text_color(index, UI_MUTED, 0);
        lv_obj_align(index, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, tr(tiles[i].title));
        lv_obj_set_style_text_color(l, UI_TEXT, 0);
        lv_obj_set_width(l, LV_PCT(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
    return scr;
}
