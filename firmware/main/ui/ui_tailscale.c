#include "ui.h"
#include "ui_softkbd.h"
#include "net/tailscale.h"
#include "net/wifi_mgr.h"
#include "settings/settings.h"
#include "servers/server_store.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "storage/sd.h"
#define fs_read_all_pub(p, o, l) fs_read_all((p), (o), (l), 4096)

static lv_obj_t *s_scr, *s_status, *s_peers;
void ts_peer_cb(lv_event_t *e);

static void fill(void)
{
    if (!s_scr) return;
    char b[200];
    snprintf(b, sizeof(b), tr(STR_TS_STATUS), tailscale_state_text());
    if (tailscale_state() == TS_CONNECTED) { char ip[80]; snprintf(ip, sizeof(ip), tr(STR_TS_VPN_IP), tailscale_vpn_ip()); strlcat(b, "\n", sizeof(b)); strlcat(b, ip, sizeof(b)); snprintf(ip, sizeof(ip), "\nDERP %u", tailscale_home_derp()); strlcat(b, ip, sizeof(b)); }
    if (tailscale_state() == TS_ERROR) { strlcat(b, "\n", sizeof(b)); strlcat(b, tailscale_last_error(), sizeof(b)); }
    lv_label_set_text(s_status, b);
    lv_obj_clean(s_peers);
    int n = tailscale_peer_count();
    if (!n) ui_label(s_peers, tr(STR_TS_NO_PEERS), FONT_SMALL, UI_MUTED);
    for (int i = 0; i < n; i++) {
        ts_peer_t p;
        if (!tailscale_peer(i, &p)) continue;
        char sub[96]; snprintf(sub, sizeof(sub), "%s  %s%s  DERP %u%s", p.ip, p.online ? "online" : "offline", p.direct ? "  direct" : "  relay", p.derp_region, p.exit_node ? "  exit" : "");
        lv_obj_t *it = ui_list_item(s_peers, p.online ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE, p.hostname, sub, NULL, NULL);
        lv_obj_set_user_data(it, (void *)(intptr_t)i);
        lv_obj_add_event_cb(it, ts_peer_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void add_server_cb(int m, void *ud)
{
    int i = (int)(intptr_t)ud;
    ts_peer_t p; if (m != 0 || !tailscale_peer(i, &p)) return;
    server_t s; memset(&s, 0, sizeof(s));
    strlcpy(s.name, p.hostname, sizeof(s.name));
    char *dot = strchr(s.name, '.'); if (dot) *dot = 0;
    strlcpy(s.host, p.hostname, sizeof(s.host));   /* keep the name; resolved at connect time */
    s.port = 22;
    int idx = server_store_add(&s);
    if (idx >= 0) ui_nav_push(ui_server_form_create, (void *)(intptr_t)idx);
}
void ts_peer_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    const char *items[] = { tr(STR_TS_ADD_SERVER) };
    ts_peer_t p; if (!tailscale_peer(i, &p)) return;
    ui_menu(p.hostname, items, 1, add_server_cb, (void *)(intptr_t)i);
}

static void ev_async(void *arg) { (void)arg; fill(); }
static void ts_ev(void *user) { (void)user; if (s_scr) ui_async(ev_async, NULL); }

static void enable_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    g_settings.ts_enable = on; settings_save();
    if (on) {
        if (!wifi_mgr_is_connected()) { ui_toast(tr(STR_WIFI_NOT_CONNECTED)); return; }
        if (!g_settings.ts_auth_key[0]) ui_alert(tr(STR_TAILSCALE), tr(STR_TS_KEY_MISSING));
        if (!tailscale_start()) ui_alert(tr(STR_ERROR), tailscale_last_error());
    } else tailscale_stop();
    fill();
}
static void key_prompt_cb(const char *v, void *ud) { (void)ud; strlcpy(g_settings.ts_auth_key, v, sizeof(g_settings.ts_auth_key)); settings_save(); ui_nav_refresh(); }
static void key_cb(lv_event_t *e) { (void)e; ui_prompt(tr(STR_TS_AUTH_KEY), "tskey-auth-", g_settings.ts_auth_key, false, key_prompt_cb, NULL); }
static void key_sd_cb(lv_event_t *e)
{
    (void)e;
    char *txt = NULL; size_t len = 0;
    if (!fs_read_all_pub("/sdcard/tailscale/authkey.txt", &txt, &len)) { ui_toast(tr(STR_CANNOT_READ)); return; }
    char *nl = strpbrk(txt, "\r\n"); if (nl) *nl = 0;
    strlcpy(g_settings.ts_auth_key, txt, sizeof(g_settings.ts_auth_key)); free(txt);
    settings_save(); ui_toast(tr(STR_TS_KEY_LOADED)); ui_nav_refresh();
}
static void name_prompt_cb(const char *v, void *ud) { (void)ud; if (v && *v) { strlcpy(g_settings.ts_device_name, v, sizeof(g_settings.ts_device_name)); settings_save(); ui_nav_refresh(); } }
static void name_cb(lv_event_t *e) { (void)e; ui_prompt(tr(STR_TS_DEVICE_NAME), "tab5", g_settings.ts_device_name, false, name_prompt_cb, NULL); }
static void ctrl_prompt_cb(const char *v, void *ud) { (void)ud; strlcpy(g_settings.ts_ctrl_host, v, sizeof(g_settings.ts_ctrl_host)); settings_save(); ui_nav_refresh(); }
static void ctrl_cb(lv_event_t *e) { (void)e; ui_prompt(tr(STR_TS_CTRL_HOST), "https://headscale.example", g_settings.ts_ctrl_host, false, ctrl_prompt_cb, NULL); }
static void direct_cb(lv_event_t *e)
{
    g_settings.ts_direct = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED); settings_save();
    ui_toast(tr(STR_REBOOT_Q));   /* takes effect after the client restarts */
}
static void forget_yes(bool yes, void *ud) { (void)ud; if (yes) { tailscale_forget_node(); g_settings.ts_auth_key[0] = 0; settings_save(); ui_nav_refresh(); } }
static void forget_cb(lv_event_t *e) { (void)e; ui_confirm(tr(STR_TS_FORGET), tr(STR_TS_FORGET_Q), forget_yes, NULL); }
static void refresh_cb(lv_event_t *e) { (void)e; fill(); }
static void del_cb(lv_event_t *e) { (void)e; tailscale_set_event_cb(NULL, NULL); s_scr = NULL; }

lv_obj_t *ui_tailscale_create(void *arg)
{
    (void)arg;
    s_scr = ui_screen_base();
    lv_obj_add_event_cb(s_scr, del_cb, LV_EVENT_DELETE, NULL);
    lv_obj_t *bar = ui_topbar(s_scr, tr(STR_TAILSCALE), true);
    ui_topbar_add_button(bar, LV_SYMBOL_REFRESH, refresh_cb, NULL);
    lv_obj_t *c = ui_content(s_scr);
    lv_obj_t *card = ui_card(c);
    s_status = ui_label(card, "", FONT_BODY, UI_TEXT);
    ui_switch_row(c, tr(STR_TS_ENABLE), g_settings.ts_enable, enable_cb, NULL);
    char masked[40] = "";
    if (g_settings.ts_auth_key[0]) snprintf(masked, sizeof(masked), "%.16s...", g_settings.ts_auth_key);
    ui_setting_row(c, tr(STR_TS_AUTH_KEY), masked[0] ? masked : tr(STR_NONE), key_cb, NULL);
    ui_setting_row(c, tr(STR_TS_KEY_FROM_SD), "", key_sd_cb, NULL);
    ui_setting_row(c, tr(STR_TS_DEVICE_NAME), g_settings.ts_device_name, name_cb, NULL);
    ui_setting_row(c, tr(STR_TS_CTRL_HOST), g_settings.ts_ctrl_host[0] ? g_settings.ts_ctrl_host : "Tailscale", ctrl_cb, NULL);
    ui_switch_row(c, tr(STR_TS_DIRECT), g_settings.ts_direct, direct_cb, NULL);
    ui_label(c, tr(STR_TS_DIRECT_NOTE), FONT_SMALL, UI_MUTED);
    ui_label(c, tr(STR_TS_NOTE), FONT_SMALL, UI_MUTED);
    ui_section_title(c, tr(STR_TS_PEERS));
    s_peers = lv_obj_create(c);
    lv_obj_set_width(s_peers, LV_PCT(100)); lv_obj_set_height(s_peers, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_peers, 0, 0); lv_obj_set_style_border_width(s_peers, 0, 0); lv_obj_set_style_pad_all(s_peers, ui_theme_pad(0), 0);
    lv_obj_set_flex_flow(s_peers, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(s_peers, ui_theme_pad(6), 0);
    ui_button_colored(c, tr(STR_TS_FORGET), UI_DANGER, forget_cb, NULL);
    tailscale_set_event_cb(ts_ev, NULL);
    fill();
    return s_scr;
}
