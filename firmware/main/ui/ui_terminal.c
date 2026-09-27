/* Full-screen SSH terminal with edge hot zones, control bar and soft keyboard. */
#include "ui.h"
#include "ui_softkbd.h"
#include "servers/server_store.h"
#include "ssh/ssh_client.h"
#include "term/vt.h"
#include "term/term_view.h"
#include "term/term_keys.h"
#include "input/keyboard.h"
#include "ime/ime.h"
#include "ime/ime_ui.h"
#include "settings/settings.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "term_ui";
#define EDGE 28
#define CTRL_W 120
#define STATUS_H 44

typedef struct {
    lv_obj_t *scr, *status, *status_lbl, *ctrl, *kbd, *hint, *ime_lbl, *close_btn;
    term_view_t *tv;
    vt_t *vt;
    ssh_client_t *ssh;
    int server_idx;
    bool ctrl_on, alt_on, kbd_shown, ctrl_shown, status_shown;
    lv_timer_t *timer;
    /* thread hand-off */
    SemaphoreHandle_t lock;
    uint8_t *rx; size_t rx_len, rx_cap;
    volatile ssh_state_t pending_state; volatile bool state_dirty; char state_msg[160];
    /* host key prompt */
    SemaphoreHandle_t hk_sem; volatile int hk_answer; char hk_fp[120]; bool hk_changed;
    bool closing;
} term_ctx_t;

static term_ctx_t *s_ctx = NULL;
bool g_ssh_session_active = false;

/* -------------------------------------------------------- ssh callbacks (ssh task) */
static void on_data(const uint8_t *d, size_t n, void *user)
{
    term_ctx_t *c = user;
    xSemaphoreTake(c->lock, portMAX_DELAY);
    if (c->rx_len + n > c->rx_cap) {
        size_t nc = c->rx_cap * 2; while (nc < c->rx_len + n) nc *= 2;
        if (nc > 1 << 20) { c->rx_len = 0; nc = c->rx_cap; }   /* drop on overflow */
        else { c->rx = realloc(c->rx, nc); c->rx_cap = nc; }
    }
    if (c->rx_len + n <= c->rx_cap) { memcpy(c->rx + c->rx_len, d, n); c->rx_len += n; }
    xSemaphoreGive(c->lock);
}
static void on_state(ssh_state_t st, const char *msg, void *user)
{
    term_ctx_t *c = user;
    xSemaphoreTake(c->lock, portMAX_DELAY);
    c->pending_state = st; c->state_dirty = true;
    if (msg) strlcpy(c->state_msg, msg, sizeof(c->state_msg)); else c->state_msg[0] = 0;
    xSemaphoreGive(c->lock);
}
static void hk_confirm_cb(bool yes, void *ud) { term_ctx_t *c = ud; c->hk_answer = yes ? 1 : 2; xSemaphoreGive(c->hk_sem); }
static void hk_show(void *arg)
{
    term_ctx_t *c = arg;
    char txt[320];
    snprintf(txt, sizeof(txt), "%s\n\nSHA256:%s", tr(c->hk_changed ? STR_SSH_HOSTKEY_CHANGED : STR_SSH_HOSTKEY_NEW), c->hk_fp);
    ui_confirm(tr(STR_CONNECT), txt, hk_confirm_cb, c);
}
static bool on_hostkey(const char *host, int port, const char *fp, bool changed, void *user)
{
    term_ctx_t *c = user;
    (void)host; (void)port;
    strlcpy(c->hk_fp, fp, sizeof(c->hk_fp)); c->hk_changed = changed; c->hk_answer = 0;
    ui_async(hk_show, c);
    xSemaphoreTake(c->hk_sem, portMAX_DELAY);
    return c->hk_answer == 1;
}

/* -------------------------------------------------------- vt callbacks */
static void vt_write_cb(const uint8_t *d, size_t n, void *user) { term_ctx_t *c = user; if (c->ssh) ssh_client_send(c->ssh, d, n); }
static void vt_bell_cb(void *user) { (void)user; }
static void vt_title_cb(const char *title, void *user)
{
    term_ctx_t *c = user;
    if (!c->closing && c->status_lbl) lv_label_set_text(c->status_lbl, title);
}

/* -------------------------------------------------------- key sink */
static void term_send_text(const char *utf8, void *user)
{
    term_ctx_t *c = user;
    if (!c->ssh) return;
    if (vt_bracketed_paste(c->vt) && strlen(utf8) > 1) {
        ssh_client_send(c->ssh, (const uint8_t *)"\x1b[200~", 6);
        ssh_client_send(c->ssh, (const uint8_t *)utf8, strlen(utf8));
        ssh_client_send(c->ssh, (const uint8_t *)"\x1b[201~", 6);
    } else ssh_client_send(c->ssh, (const uint8_t *)utf8, strlen(utf8));
    vt_scroll_view_reset(c->vt);
}
static bool term_key(const key_event_t *ev, void *user)
{
    term_ctx_t *c = user;
    key_event_t e = *ev;
    if (c->ctrl_on) e.mods |= MOD_CTRL;
    if (c->alt_on) e.mods |= MOD_ALT;
    if (ev->type == KEY_EV_SPECIAL && (ev->key == SK_PGUP || ev->key == SK_PGDN) && (ev->mods & MOD_SHIFT)) {
        vt_scroll_view(c->vt, ev->key == SK_PGUP ? vt_rows(c->vt) / 2 : -vt_rows(c->vt) / 2);
        lv_obj_invalidate(term_view_obj(c->tv));
        return true;
    }
    char buf[32];
    size_t n = term_keys_encode(&e, c->vt, buf, sizeof(buf));
    if (n && c->ssh) { ssh_client_send(c->ssh, (const uint8_t *)buf, n); vt_scroll_view_reset(c->vt); }
    if (c->ctrl_on || c->alt_on) { c->ctrl_on = c->alt_on = false; }
    return true;
}

/* -------------------------------------------------------- layout */
static void layout(term_ctx_t *c)
{
    int W = lv_display_get_horizontal_resolution(NULL), H = lv_display_get_vertical_resolution(NULL);
    int x = 0, y = 0, w = W, h = H;
    if (c->status_shown) { lv_obj_remove_flag(c->status, LV_OBJ_FLAG_HIDDEN); y += STATUS_H; h -= STATUS_H; } else lv_obj_add_flag(c->status, LV_OBJ_FLAG_HIDDEN);
    if (c->ctrl_shown) { lv_obj_remove_flag(c->ctrl, LV_OBJ_FLAG_HIDDEN); lv_obj_set_pos(c->ctrl, 0, y); lv_obj_set_height(c->ctrl, h); x += CTRL_W; w -= CTRL_W; } else lv_obj_add_flag(c->ctrl, LV_OBJ_FLAG_HIDDEN);
    if (c->kbd_shown && softkbd_visible()) { h -= softkbd_height(); }
    lv_obj_set_pos(term_view_obj(c->tv), x, y);
    lv_obj_set_size(term_view_obj(c->tv), w, h);
    int cols, rows;
    if (term_view_fit(c->tv, &cols, &rows)) {
        vt_resize(c->vt, cols, rows);
        if (c->ssh) ssh_client_resize(c->ssh, cols, rows);
    }
    ime_ui_set_anchor(c->kbd_shown ? H - softkbd_height() : -1);
    lv_obj_invalidate(term_view_obj(c->tv));
}

static void toggle_kbd(term_ctx_t *c)
{
    if (c->kbd_shown) { softkbd_hide_all(); c->kbd = NULL; c->kbd_shown = false; c->ctrl_shown = false; }
    else { c->kbd = softkbd_create(c->scr); c->kbd_shown = true; c->ctrl_shown = true; }
    layout(c);
}

/* -------------------------------------------------------- control bar */
typedef struct { const char *label; special_key_t sk; int kind; } ctrl_key_t;  /* kind 0=special 1=ctrl toggle 2=alt toggle 3=ime 4=kbd 5=scroll up 6=scroll down */
static const ctrl_key_t s_ctrl_keys[] = {
    { "ESC", SK_ESC, 0 }, { "TAB", SK_TAB, 0 }, { "CTRL", 0, 1 }, { "ALT", 0, 2 },
    { LV_SYMBOL_UP, SK_UP, 0 }, { LV_SYMBOL_DOWN, SK_DOWN, 0 }, { LV_SYMBOL_LEFT, SK_LEFT, 0 }, { LV_SYMBOL_RIGHT, SK_RIGHT, 0 },
    { "HOME", SK_HOME, 0 }, { "END", SK_END, 0 }, { "PgUp", SK_PGUP, 0 }, { "PgDn", SK_PGDN, 0 },
    { "F1", SK_F1, 0 }, { "F2", SK_F2, 0 }, { "F5", SK_F5, 0 }, { "F10", SK_F10, 0 },
    { "^C", 0, 7 }, { "^Z", 0, 8 }, { "^D", 0, 9 }, { "Enter", SK_ENTER, 0 },
    { "A/あ", 0, 3 }, { LV_SYMBOL_KEYBOARD, 0, 4 },
};

static void ctrl_key_cb(lv_event_t *e)
{
    term_ctx_t *c = s_ctx;
    const ctrl_key_t *k = lv_event_get_user_data(e);
    lv_obj_t *b = lv_event_get_target_obj(e);
    switch (k->kind) {
    case 0: { key_event_t ev = { .type = KEY_EV_SPECIAL, .key = k->sk }; term_key(&ev, c); break; }
    case 1: c->ctrl_on = !c->ctrl_on; break;
    case 2: c->alt_on = !c->alt_on; break;
    case 3: ime_toggle_source(); break;
    case 4: toggle_kbd(c); return;
    case 7: case 8: case 9: { key_event_t ev = { .type = KEY_EV_CHAR, .cp = k->kind == 7 ? 'c' : k->kind == 8 ? 'z' : 'd', .mods = MOD_CTRL }; term_key(&ev, c); break; }
    }
    (void)b;
    /* refresh toggle colours */
    uint32_t n = lv_obj_get_child_count(c->ctrl);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *ch = lv_obj_get_child(c->ctrl, i);
        const ctrl_key_t *kk = lv_obj_get_user_data(ch);
        if (!kk) continue;
        bool on = (kk->kind == 1 && c->ctrl_on) || (kk->kind == 2 && c->alt_on) || (kk->kind == 3 && ime_is_active());
        lv_obj_set_style_bg_color(ch, on ? UI_ACCENT : UI_CARD_HI, 0);
    }
}

static lv_obj_t *build_ctrl(term_ctx_t *c)
{
    lv_obj_t *bar = lv_obj_create(c->scr);
    lv_obj_set_size(bar, CTRL_W, LV_PCT(100));
    lv_obj_set_style_bg_color(bar, UI_CARD, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, ui_theme_radius(0), 0);
    lv_obj_set_style_pad_all(bar, ui_theme_pad(6), 0);
    lv_obj_set_style_pad_row(bar, ui_theme_pad(6), 0);
    lv_obj_set_style_pad_column(bar, ui_theme_pad(6), 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scroll_dir(bar, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);
    for (size_t i = 0; i < sizeof(s_ctrl_keys) / sizeof(s_ctrl_keys[0]); i++) {
        lv_obj_t *b = lv_button_create(bar);
        lv_obj_set_size(b, 51, 44);
        lv_obj_set_style_radius(b, ui_theme_radius(8), 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_pad_all(b, ui_theme_pad(0), 0);
        lv_obj_set_style_bg_color(b, UI_CARD_HI, 0);
        lv_obj_set_user_data(b, (void *)&s_ctrl_keys[i]);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, s_ctrl_keys[i].label);
        lv_obj_set_style_text_font(l, FONT_SMALL, 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, ctrl_key_cb, LV_EVENT_CLICKED, (void *)&s_ctrl_keys[i]);
    }
    return bar;
}

/* -------------------------------------------------------- status bar */
static void close_confirm_cb(bool yes, void *ud) { (void)ud; if (yes) ui_nav_back(); }
static void close_cb(lv_event_t *e) { (void)e; ui_confirm(tr(STR_DISCONNECT), tr(STR_TERM_CLOSE_Q), close_confirm_cb, NULL); }

/* The terminal keeps the horizontal axis to itself: a stray swipe must not walk
 * out of a live session, and dragging already scrolls the scrollback. The
 * shortcuts that a swipe would otherwise carry live in a long-press menu. */
static void term_gesture_cb(lv_dir_t dir) { (void)dir; }

static void term_menu_cb(int item, void *ud)
{
    term_ctx_t *c = ud;
    switch (item) {
    case 0: ui_confirm(tr(STR_DISCONNECT), tr(STR_TERM_CLOSE_Q), close_confirm_cb, NULL); break;
    case 1: vt_reset(c->vt); term_view_refresh(c->tv); break;
    case 2: vt_scroll_view_reset(c->vt); term_view_refresh(c->tv); break;
    case 3: ime_toggle_source(); break;
    }
}

static void term_long_cb(lv_event_t *e)
{
    term_ctx_t *c = lv_event_get_user_data(e);
    if (lv_indev_active() && !term_view_long_press_allowed(c->tv)) return;
    const char *items[] = { tr(STR_DISCONNECT), tr(STR_TERM_CLEAR), tr(STR_TERM_SCROLL_END), tr(STR_IME_MODE) };
    ui_menu(tr(STR_TERM_MENU), items, 4, term_menu_cb, c);
}

static lv_obj_t *build_status(term_ctx_t *c)
{
    lv_obj_t *bar = lv_obj_create(c->scr);
    lv_obj_set_size(bar, LV_PCT(100), STATUS_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, UI_CARD, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, ui_theme_radius(0), 0);
    lv_obj_set_style_pad_all(bar, ui_theme_pad(4), 0);
    lv_obj_set_style_pad_column(bar, ui_theme_pad(10), 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    c->close_btn = lv_button_create(bar);
    lv_obj_set_size(c->close_btn, 56, 36);
    lv_obj_set_style_bg_color(c->close_btn, UI_DANGER, 0);
    lv_obj_set_style_shadow_width(c->close_btn, 0, 0);
    lv_obj_t *xl = lv_label_create(c->close_btn); lv_label_set_text(xl, LV_SYMBOL_CLOSE); lv_obj_center(xl);
    lv_obj_add_event_cb(c->close_btn, close_cb, LV_EVENT_CLICKED, NULL);
    c->status_lbl = lv_label_create(bar);
    lv_obj_set_flex_grow(c->status_lbl, 1);
    lv_label_set_long_mode(c->status_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(c->status_lbl, FONT_SMALL, 0);
    c->ime_lbl = lv_label_create(bar);
    lv_label_set_text(c->ime_lbl, ime_status_label());
    lv_obj_set_style_text_color(c->ime_lbl, UI_ACCENT, 0);
    lv_obj_t *st = lv_label_create(bar);
    char buf[128]; ui_status_text(buf, sizeof(buf));
    lv_label_set_text(st, buf);
    lv_obj_set_style_text_font(st, FONT_SMALL, 0);
    lv_obj_set_style_text_color(st, UI_MUTED, 0);
    return bar;
}

/* -------------------------------------------------------- hot zones */
static void hotzone_cb(lv_event_t *e)
{
    term_ctx_t *c = s_ctx;
    int zone = (int)(intptr_t)lv_event_get_user_data(e);
    if (zone == 0) { c->status_shown = !c->status_shown; }
    else if (zone == 1) { c->ctrl_shown = !c->ctrl_shown; }
    else if (zone == 2) {
        if (keyboard_any_present() && !c->kbd_shown) { c->ctrl_shown = !c->ctrl_shown; ESP_LOGI(TAG, "Hotzone right: physical keyboard present, soft keyboard suppressed"); }
        else { toggle_kbd(c); return; }
    }
    layout(c);
}
static lv_obj_t *hotzone(lv_obj_t *scr, int zone)
{
    lv_obj_t *z = lv_obj_create(scr);
    lv_obj_set_style_bg_opa(z, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(z, 0, 0);
    lv_obj_remove_flag(z, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(z, LV_OBJ_FLAG_CLICKABLE);
    int W = lv_display_get_horizontal_resolution(NULL), H = lv_display_get_vertical_resolution(NULL);
    if (zone == 0) { lv_obj_set_size(z, W - 2 * EDGE - 200, EDGE); lv_obj_align(z, LV_ALIGN_TOP_MID, 0, 0); }
    else if (zone == 1) { lv_obj_set_size(z, EDGE, H - 200); lv_obj_align(z, LV_ALIGN_LEFT_MID, 0, 0); }
    else { lv_obj_set_size(z, EDGE, H - 200); lv_obj_align(z, LV_ALIGN_RIGHT_MID, 0, 0); }
    lv_obj_add_event_cb(z, hotzone_cb, LV_EVENT_CLICKED, (void *)(intptr_t)zone);
    return z;
}

/* -------------------------------------------------------- periodic pump */
static void pump_cb(lv_timer_t *t)
{
    term_ctx_t *c = lv_timer_get_user_data(t);
    if (!c || c->closing) return;
    xSemaphoreTake(c->lock, portMAX_DELAY);
    size_t n = c->rx_len; uint8_t *buf = NULL;
    if (n) { buf = malloc(n); memcpy(buf, c->rx, n); c->rx_len = 0; }
    bool sd = c->state_dirty; ssh_state_t st = c->pending_state; char msg[160]; strlcpy(msg, c->state_msg, sizeof(msg));
    c->state_dirty = false;
    xSemaphoreGive(c->lock);
    if (buf) { vt_feed(c->vt, buf, n); free(buf); term_view_refresh(c->tv); }
    if (sd) {
        const char *txt = NULL; char tmp[200];
        switch (st) {
        case SSH_ST_CONNECTING: txt = tr(STR_CONNECTING); break;
        case SSH_ST_AUTH: txt = tr(STR_CONNECTING); break;
        case SSH_ST_CONNECTED: txt = tr(STR_CONNECTED); g_ssh_session_active = true; break;
        case SSH_ST_CLOSED: txt = tr(STR_DISCONNECTED); g_ssh_session_active = false; break;
        case SSH_ST_ERROR: snprintf(tmp, sizeof(tmp), "%s %s", tr(STR_SSH_CONN_FAILED), msg); txt = tmp; g_ssh_session_active = false; break;
        default: break;
        }
        if (txt) {
            char line[260]; snprintf(line, sizeof(line), "\r\n\x1b[1;%dm[%s]\x1b[0m\r\n", st == SSH_ST_ERROR ? 31 : 32, txt);
            vt_feed(c->vt, (const uint8_t *)line, strlen(line));
            term_view_refresh(c->tv);
            if (c->status_lbl) {
                const server_t *s = server_store_get(c->server_idx);
                snprintf(line, sizeof(line), "%s  -  %s", s ? s->name : "", txt);
                lv_label_set_text(c->status_lbl, line);
            }
            if (st == SSH_ST_ERROR || st == SSH_ST_CLOSED) { c->status_shown = true; layout(c); }
        }
    }
    if (c->ime_lbl) lv_label_set_text(c->ime_lbl, ime_status_label());
}

/* -------------------------------------------------------- lifecycle */
static void scr_delete_cb(lv_event_t *e)
{
    term_ctx_t *c = lv_event_get_user_data(e);
    c->closing = true;
    keyboard_set_sink(NULL);
    ime_ui_set_indicator(NULL);
    ime_ui_set_anchor(-1);
    if (c->timer) lv_timer_delete(c->timer);
    if (c->ssh) { ssh_client_stop(c->ssh); c->ssh = NULL; }
    g_ssh_session_active = false;
    /* term view frees itself with the object; vt after */
    vt_destroy(c->vt);
    free(c->rx);
    vSemaphoreDelete(c->lock);
    vSemaphoreDelete(c->hk_sem);
    if (s_ctx == c) s_ctx = NULL;
    free(c);
}

lv_obj_t *ui_terminal_create(void *arg)
{
    int idx = (int)(intptr_t)arg;
    const server_t *s = server_store_get(idx);
    if (!s) return ui_servers_create(NULL);
    term_ctx_t *c = calloc(1, sizeof(*c));
    s_ctx = c;
    c->server_idx = idx;
    c->lock = xSemaphoreCreateMutex();
    c->hk_sem = xSemaphoreCreateBinary();
    c->rx_cap = 16384; c->rx = malloc(c->rx_cap);
    c->scr = ui_screen_base();
    lv_obj_set_style_bg_color(c->scr, lv_color_hex(g_settings.term_bg), 0);
    lv_obj_add_event_cb(c->scr, scr_delete_cb, LV_EVENT_DELETE, c);

    int W = lv_display_get_horizontal_resolution(NULL), H = lv_display_get_vertical_resolution(NULL);
    int cols = W / 12, rows = H / 26;   /* PixelMplus 24 px: 12 px cells */
    c->vt = vt_create(cols, rows, 1500);
    vt_set_write_cb(c->vt, vt_write_cb, c);
    vt_set_bell_cb(c->vt, vt_bell_cb, c);
    vt_set_title_cb(c->vt, vt_title_cb, c);
    c->tv = term_view_create(c->scr, c->vt, &g_term_font);
    /* Terminal theme */
    vt_set_palette(g_settings.term_ansi);
    term_view_set_colors(c->tv, lv_color_hex(g_settings.term_fg), lv_color_hex(g_settings.term_bg));
    term_view_set_cursor(c->tv, (term_cursor_t)g_settings.term_cursor_style,
                         g_settings.term_cursor_blink != 0, lv_color_hex(g_settings.term_cursor));
    ui_screen_set_gesture(c->scr, term_gesture_cb);
    lv_obj_add_event_cb(term_view_obj(c->tv), term_long_cb, LV_EVENT_LONG_PRESSED, c);
    c->status = build_status(c);
    c->ctrl = build_ctrl(c);
    c->status_shown = true;
    c->ctrl_shown = false;
    hotzone(c->scr, 0); hotzone(c->scr, 1); hotzone(c->scr, 2);
    layout(c);
    term_view_fit(c->tv, &cols, &rows);
    vt_resize(c->vt, cols, rows);

    char banner[200];
    snprintf(banner, sizeof(banner), "\x1b[36m%s %s@%s:%d ...\x1b[0m\r\n\x1b[90m%s\x1b[0m\r\n", tr(STR_CONNECTING), s->user, s->host, s->port, tr(STR_TERM_HINT));
    vt_feed(c->vt, (const uint8_t *)banner, strlen(banner));

    key_sink_t sink = { .key = term_key, .text = term_send_text, .user = c };
    keyboard_set_sink(&sink);
    ime_ui_set_indicator(c->ime_lbl);

    ssh_params_t p = { 0 };
    strlcpy(p.host, s->host, sizeof(p.host)); p.port = s->port;
    strlcpy(p.user, s->user, sizeof(p.user)); strlcpy(p.pass, s->pass, sizeof(p.pass));
    strlcpy(p.keypath, s->keypath, sizeof(p.keypath)); p.use_key = s->use_key;
    p.cols = cols; p.rows = rows;
    c->ssh = ssh_client_start(&p, on_data, on_state, on_hostkey, c);
    c->timer = lv_timer_create(pump_cb, 16, c);
    if (!keyboard_any_present()) { /* show soft keyboard by default on touch-only devices */ }
    return c->scr;
}
