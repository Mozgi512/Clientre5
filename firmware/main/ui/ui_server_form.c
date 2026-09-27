#include "ui.h"
#include "ui_softkbd.h"
#include "servers/server_store.h"
#include "input/keyboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int idx; lv_obj_t *name, *host, *port, *user, *pass, *key, *use_key; lv_obj_t *scr; } form_t;
static form_t s_f;

static void save_cb(lv_event_t *e)
{
    (void)e;
    server_t s; memset(&s, 0, sizeof(s));
    strlcpy(s.name, lv_textarea_get_text(s_f.name), sizeof(s.name));
    strlcpy(s.host, lv_textarea_get_text(s_f.host), sizeof(s.host));
    s.port = atoi(lv_textarea_get_text(s_f.port)); if (s.port <= 0) s.port = 22;
    strlcpy(s.user, lv_textarea_get_text(s_f.user), sizeof(s.user));
    strlcpy(s.pass, lv_textarea_get_text(s_f.pass), sizeof(s.pass));
    strlcpy(s.keypath, lv_textarea_get_text(s_f.key), sizeof(s.keypath));
    s.use_key = lv_obj_has_state(s_f.use_key, LV_STATE_CHECKED);
    if (!s.host[0] || !s.user[0]) { ui_toast(tr(STR_HOST_USER_REQ)); return; }
    if (!s.name[0]) snprintf(s.name, sizeof(s.name), "%s@%s", s.user, s.host);
    bool saved = s_f.idx >= 0 ? server_store_update(s_f.idx, &s) : server_store_add(&s) >= 0;
    if (!saved) { ui_alert(tr(STR_ERROR), tr(STR_SERVER_SAVE_FAILED)); return; }
    ui_toast(tr(STR_SAVED));
    ui_nav_back();
}

static lv_obj_t *field(lv_obj_t *parent, const char *label, const char *value, bool pw)
{
    ui_section_title(parent, label);
    lv_obj_t *ta = ui_textarea(parent, label, pw, true);
    if (value) lv_textarea_set_text(ta, value);
    softkbd_attach_textarea(ta, s_f.scr);
    return ta;
}

lv_obj_t *ui_server_form_create(void *arg)
{
    int idx = (int)(intptr_t)arg;
    const server_t *s = server_store_get(idx);
    memset(&s_f, 0, sizeof(s_f));
    s_f.idx = idx;
    lv_obj_t *scr = ui_screen_base();
    s_f.scr = scr;
    lv_obj_t *bar = ui_topbar(scr, tr(s ? STR_EDIT_SERVER : STR_ADD_SERVER), true);
    ui_topbar_add_button(bar, LV_SYMBOL_SAVE, save_cb, NULL);
    lv_obj_t *c = ui_content(scr);
    lv_obj_set_style_pad_bottom(c, softkbd_height() + 20, 0);
    char port[8]; snprintf(port, sizeof(port), "%d", s ? s->port : 22);
    s_f.name = field(c, tr(STR_NAME), s ? s->name : "", false);
    s_f.host = field(c, tr(STR_HOST), s ? s->host : "", false);
    s_f.port = field(c, tr(STR_PORT), port, false);
    s_f.user = field(c, tr(STR_USERNAME), s ? s->user : "", false);
    s_f.pass = field(c, tr(STR_PASSWORD), s ? s->pass : "", true);
    lv_obj_t *sw = ui_switch_row(c, tr(STR_AUTH_KEY), s ? s->use_key : false, NULL, NULL);
    s_f.use_key = lv_obj_get_user_data(sw);
    s_f.key = field(c, tr(STR_KEY_PATH), s && s->keypath[0] ? s->keypath : "/sdcard/.ssh/id_ed25519", false);
    ui_button(c, tr(STR_SAVE), save_cb, NULL);
    return scr;
}
