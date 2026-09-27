#include "ui.h"
#include "web/webfm.h"
#include "net/wifi_mgr.h"
#include "settings/settings.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_lbl, *s_qr;
static void refresh(void)
{
    char ip[48]; wifi_mgr_get_ip4(ip, sizeof(ip));
    char buf[160];
    if (webfm_running() && ip[0]) { snprintf(buf, sizeof(buf), tr(STR_WEBFM_RUNNING), ip); char url[80]; snprintf(url, sizeof(url), "http://%s/", ip); lv_qrcode_update(s_qr, url, strlen(url)); lv_obj_set_hidden(s_qr, false); }
    else { snprintf(buf, sizeof(buf), "%s", tr(STR_WEBFM_STOPPED)); lv_obj_set_hidden(s_qr, true); }
    lv_label_set_text(s_lbl, buf);
}
static void start_cb(lv_event_t *e) { (void)e; if (!wifi_mgr_is_connected()) { ui_toast(tr(STR_WEBFM_NEED_WIFI)); return; } if (!webfm_start()) ui_toast(tr(STR_FAILED)); refresh(); }
static void stop_cb(lv_event_t *e) { (void)e; webfm_stop(); refresh(); }
static void auto_cb(lv_event_t *e) { g_settings.webfm_autostart = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED); settings_save(); }

lv_obj_t *ui_webfm_create(void *arg)
{
    (void)arg;
    lv_obj_t *scr = ui_screen_base();
    ui_topbar(scr, tr(STR_WEBFM), true);
    lv_obj_t *c = ui_content(scr);
    lv_obj_t *card = ui_card(c);
    s_lbl = ui_label(card, "", FONT_BODY, UI_TEXT);
    s_qr = lv_qrcode_create(card);
    lv_qrcode_set_size(s_qr, 180);
    lv_qrcode_set_dark_color(s_qr, lv_color_black());
    lv_qrcode_set_light_color(s_qr, lv_color_white());
    lv_obj_set_style_border_color(s_qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_qr, 8, 0);
    lv_obj_t *row = ui_row(card);
    ui_button(row, tr(STR_WEBFM_START), start_cb, NULL);
    ui_button_colored(row, tr(STR_WEBFM_STOP), UI_CARD_HI, stop_cb, NULL);
    ui_switch_row(c, "Auto start", g_settings.webfm_autostart, auto_cb, NULL);
    ui_label(c, tr(STR_WEBFM_NOTE), FONT_SMALL, UI_MUTED);
    ui_label(c, tr(STR_POWER_NOTE), FONT_SMALL, UI_MUTED);
    refresh();
    return scr;
}
