#include "ui.h"
#include "ui_softkbd.h"
#include "net/wifi_mgr.h"
#include "sys/sysinfo.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_scr, *s_status, *s_scan_list, *s_saved_list, *s_busy;

static void refresh_status(void)
{
    if (!s_scr || !s_status) return;
    char ip4[48], ip6[64], buf[400];
    wifi_mgr_get_ip4(ip4, sizeof(ip4)); wifi_mgr_get_ip6(ip6, sizeof(ip6));
    if (wifi_mgr_is_connected())
        snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %s\n%s: %s\n%s: %s\n%s: %d dBm   %s: %d", wifi_mgr_ssid(),
                 tr(STR_IPV4), ip4, tr(STR_IPV6), ip6[0] ? ip6 : "-", tr(STR_RSSI), wifi_mgr_rssi(), tr(STR_CHANNEL), wifi_mgr_channel());
    else snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s", tr(STR_WIFI_NOT_CONNECTED));
    lv_label_set_text(s_status, buf);
}

static void connect_saved_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    const wifi_saved_t *s = wifi_mgr_saved(i);
    if (!s) return;
    wifi_mgr_connect(s->ssid, s->pass, true);
    ui_toast(tr(STR_CONNECTING));
}
static void forget_cb(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    wifi_mgr_forget((int)(intptr_t)lv_event_get_user_data(e));
    ui_toast(tr(STR_WIFI_SAVED_DELETED));
    ui_nav_refresh();
}

static void fill_saved(void)
{
    lv_obj_clean(s_saved_list);
    int n = wifi_mgr_saved_count();
    if (!n) ui_label(s_saved_list, tr(STR_NONE), FONT_SMALL, UI_MUTED);
    for (int i = 0; i < n; i++) {
        const wifi_saved_t *s = wifi_mgr_saved(i);
        lv_obj_t *it = ui_list_item(s_saved_list, LV_SYMBOL_WIFI, s->ssid, s->pass[0] ? "" : "(open)", connect_saved_cb, (void *)(intptr_t)i);
        lv_obj_t *b = lv_button_create(it);
        lv_obj_set_size(b, 48, 40);
        lv_obj_set_style_bg_color(b, UI_DANGER, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, LV_SYMBOL_TRASH); lv_obj_center(l);
        lv_obj_add_event_cb(b, forget_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

typedef struct { char ssid[33]; bool open; } pick_t;
static pick_t s_pick;
static void pw_cb(const char *pw, void *ud)
{
    (void)ud;
    wifi_mgr_connect(s_pick.ssid, pw, true);
    ui_toast(tr(STR_CONNECTING));
}
static void scan_item_cb(lv_event_t *e)
{
    wifi_scan_item_t *it = lv_event_get_user_data(e);
    strlcpy(s_pick.ssid, it->ssid, sizeof(s_pick.ssid));
    s_pick.open = (it->auth == 0);
    /* reuse saved password if we have one */
    for (int i = 0; i < wifi_mgr_saved_count(); i++)
        if (!strcmp(wifi_mgr_saved(i)->ssid, it->ssid)) { wifi_mgr_connect(it->ssid, wifi_mgr_saved(i)->pass, true); ui_toast(tr(STR_CONNECTING)); return; }
    if (s_pick.open) { wifi_mgr_connect(it->ssid, "", true); ui_toast(tr(STR_CONNECTING)); return; }
    ui_prompt(it->ssid, tr(STR_PASSWORD), "", true, pw_cb, NULL);
}

static wifi_scan_item_t s_items[24];
static void fill_scan(void)
{
    if (!s_scr) return;
    lv_obj_clean(s_scan_list);
    int n = wifi_mgr_scan_results(s_items, 24);
    if (!n) ui_label(s_scan_list, tr(STR_NONE), FONT_SMALL, UI_MUTED);
    for (int i = 0; i < n; i++) {
        char sub[64]; snprintf(sub, sizeof(sub), "%d dBm  ch%d  %s", s_items[i].rssi, s_items[i].channel, s_items[i].auth ? LV_SYMBOL_EYE_CLOSE : "open");
        ui_list_item(s_scan_list, LV_SYMBOL_WIFI, s_items[i].ssid, sub, scan_item_cb, &s_items[i]);
    }
}

static void ev_async(void *arg)
{
    wifi_ev_t ev = (wifi_ev_t)(intptr_t)arg;
    if (!s_scr) return;
    if (ev == WIFI_EV_SCAN_DONE) { ui_busy_close(s_busy); s_busy = NULL; fill_scan(); }
    else if (ev == WIFI_EV_CONNECTED) { ui_toast(tr(STR_CONNECTED)); refresh_status(); fill_saved(); sysinfo_start_sntp(); }
    else if (ev == WIFI_EV_CONNECT_FAILED) { ui_toast(tr(STR_WIFI_CONNECT_FAILED)); refresh_status(); }
    else refresh_status();
}
static void wifi_ev2(wifi_ev_t ev, void *user) { (void)user; ui_async(ev_async, (void *)(intptr_t)ev); }

static void scan_cb(lv_event_t *e)
{
    (void)e;
    if (wifi_mgr_scan_start()) { if (s_busy) ui_busy_close(s_busy); s_busy = ui_busy(tr(STR_WIFI_SCANNING)); }
}
static void manual_pw_cb(const char *pw, void *ud) { (void)ud; wifi_mgr_connect(s_pick.ssid, pw, true); ui_toast(tr(STR_CONNECTING)); }
static void manual_ssid_cb(const char *ssid, void *ud)
{
    (void)ud;
    if (!ssid || !*ssid) return;
    strlcpy(s_pick.ssid, ssid, sizeof(s_pick.ssid));
    ui_prompt(tr(STR_WIFI_OPEN_NOTE), tr(STR_PASSWORD), "", true, manual_pw_cb, NULL);
}
static void manual_cb(lv_event_t *e) { (void)e; ui_prompt(tr(STR_WIFI_MANUAL), tr(STR_WIFI_SSID), "", false, manual_ssid_cb, NULL); }
static void disconnect_cb(lv_event_t *e) { (void)e; wifi_mgr_disconnect(); }
static void timer_cb(lv_timer_t *t) { (void)t; refresh_status(); }
static void del_cb(lv_event_t *e) { (void)e; wifi_mgr_set_cb(NULL, NULL); s_scr = NULL; s_status = NULL; s_scan_list = NULL; s_saved_list = NULL; s_busy = NULL; }

lv_obj_t *ui_wifi_create(void *arg)
{
    (void)arg;
    s_scr = ui_screen_base();
    lv_obj_add_event_cb(s_scr, del_cb, LV_EVENT_DELETE, NULL);
    lv_obj_t *bar = ui_topbar(s_scr, tr(STR_WIFI_MANAGER), true);
    ui_topbar_add_button(bar, LV_SYMBOL_REFRESH, scan_cb, NULL);
    ui_topbar_add_button(bar, LV_SYMBOL_EDIT, manual_cb, NULL);
    lv_obj_t *c = ui_content(s_scr);
    lv_obj_t *card = ui_card(c);
    s_status = ui_label(card, "", FONT_BODY, UI_TEXT);
    lv_obj_t *row = ui_row(card);
    ui_button_colored(row, tr(STR_DISCONNECT), UI_CARD_HI, disconnect_cb, NULL);
    ui_label(card, tr(STR_POWER_NOTE), FONT_SMALL, UI_MUTED);
    ui_section_title(c, tr(STR_WIFI_SAVED));
    s_saved_list = lv_obj_create(c);
    lv_obj_set_width(s_saved_list, LV_PCT(100)); lv_obj_set_height(s_saved_list, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_saved_list, 0, 0); lv_obj_set_style_border_width(s_saved_list, 0, 0); lv_obj_set_style_pad_all(s_saved_list, ui_theme_pad(0), 0);
    lv_obj_set_flex_flow(s_saved_list, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(s_saved_list, ui_theme_pad(8), 0);
    ui_section_title(c, tr(STR_WIFI_SCAN));
    s_scan_list = lv_obj_create(c);
    lv_obj_set_width(s_scan_list, LV_PCT(100)); lv_obj_set_height(s_scan_list, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_scan_list, 0, 0); lv_obj_set_style_border_width(s_scan_list, 0, 0); lv_obj_set_style_pad_all(s_scan_list, ui_theme_pad(0), 0);
    lv_obj_set_flex_flow(s_scan_list, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(s_scan_list, ui_theme_pad(8), 0);
    fill_saved(); fill_scan(); refresh_status();
    wifi_mgr_set_cb(wifi_ev2, NULL);
    ui_timer_bind(lv_timer_create(timer_cb, 3000, NULL), s_scr);
    return s_scr;
}
