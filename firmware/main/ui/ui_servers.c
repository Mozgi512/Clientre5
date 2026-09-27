#include "ui.h"
#include "servers/server_store.h"
#include "net/wifi_mgr.h"
#include <stdio.h>

static void add_cb(lv_event_t *e) { (void)e; ui_nav_push(ui_server_form_create, (void *)(intptr_t)-1); }

static void do_connect(int idx)
{
    if (!wifi_mgr_is_connected()) { ui_toast(tr(STR_WIFI_NOT_CONNECTED)); return; }
    ui_nav_push(ui_terminal_create, (void *)(intptr_t)idx);
}
static void connect_cb(lv_event_t *e) { do_connect((int)(intptr_t)lv_event_get_user_data(e)); }

static void del_confirm_cb(bool yes, void *ud)
{
    if (yes) { server_store_remove((int)(intptr_t)ud); ui_nav_refresh(); }
}

static void menu_cb(int item, void *ud)
{
    int idx = (int)(intptr_t)ud;
    switch (item) {
    case 0: do_connect(idx); break;
    case 1: ui_nav_push(ui_server_form_create, ud); break;
    case 2: ui_confirm(tr(STR_DEL_SERVER), tr(STR_DEL_SERVER_Q), del_confirm_cb, ud); break;
    case 3: server_store_move(idx, idx - 1); ui_nav_refresh(); break;
    case 4: server_store_move(idx, idx + 1); ui_nav_refresh(); break;
    }
}

static void long_cb(lv_event_t *e)
{
    void *ud = lv_event_get_user_data(e);
    const char *items[] = { tr(STR_CONNECT), tr(STR_EDIT), tr(STR_DELETE), LV_SYMBOL_UP, LV_SYMBOL_DOWN };
    ui_menu(server_store_get((int)(intptr_t)ud)->name, items, 5, menu_cb, ud);
}

static void more_cb(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    long_cb(e);
}

static void status_timer(lv_timer_t *t)
{
    lv_obj_t *l = lv_timer_get_user_data(t);
    char ip4[48], ip6[64];
    wifi_mgr_get_ip4(ip4, sizeof(ip4)); wifi_mgr_get_ip6(ip6, sizeof(ip6));
    char buf[200];
    if (wifi_mgr_is_connected()) snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %s   IPv4 %s   IPv6 %s", wifi_mgr_ssid(), ip4, ip6[0] ? ip6 : "-");
    else snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s", tr(STR_WIFI_NOT_CONNECTED));
    lv_label_set_text(l, buf);
}

lv_obj_t *ui_servers_create(void *arg)
{
    (void)arg;
    lv_obj_t *scr = ui_screen_base();
    lv_obj_t *bar = ui_topbar(scr, tr(STR_SERVERS), true);
    ui_topbar_add_button(bar, LV_SYMBOL_PLUS, add_cb, NULL);
    lv_obj_t *c = ui_content(scr);
    lv_obj_t *st = ui_label(c, "", FONT_SMALL, UI_MUTED);
    lv_timer_t *t = lv_timer_create(status_timer, 2000, st);
    ui_timer_bind(t, scr);
    status_timer(t);
    int n = server_store_count();
    if (n == 0) ui_label(c, tr(STR_NO_SERVERS), FONT_BODY, UI_MUTED);
    for (int i = 0; i < n; i++) {
        const server_t *s = server_store_get(i);
        char sub[220]; snprintf(sub, sizeof(sub), "%s@%s:%d%s", s->user, s->host, s->port, s->use_key ? "  " LV_SYMBOL_OK "key" : "");
        lv_obj_t *item = ui_list_item(c, LV_SYMBOL_KEYBOARD, s->name[0] ? s->name : s->host, sub, connect_cb, (void *)(intptr_t)i);
        lv_obj_add_event_cb(item, long_cb, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);
        lv_obj_t *more = lv_button_create(item);
        lv_obj_set_size(more, 48, 40);
        lv_obj_set_style_bg_color(more, UI_CARD_HI, 0);
        lv_obj_set_style_shadow_width(more, 0, 0);
        lv_obj_t *ml = lv_label_create(more); lv_label_set_text(ml, LV_SYMBOL_LIST); lv_obj_center(ml);
        lv_obj_add_event_cb(more, more_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    return scr;
}
