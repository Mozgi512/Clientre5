#include "ui.h"
#include "storage/sd.h"
#include "storage/backup.h"
#include "esp_system.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static bool pw_ok(const char *pw)
{
    size_t n = strlen(pw);
    if (n < 4 || n > 64) return false;
    for (size_t i = 0; i < n; i++) if (!isalnum((unsigned char)pw[i])) return false;
    return true;
}
static void export_pw_cb(const char *pw, void *ud)
{
    (void)ud;
    if (!pw_ok(pw)) { ui_alert(tr(STR_BACKUP_PASSWORD), tr(STR_BACKUP_PW_NOTE)); return; }
    char path[96], err[96];
    if (backup_export(pw, path, sizeof(path), err, sizeof(err))) ui_toastf(tr(STR_BACKUP_WRITTEN), path); else ui_alert(tr(STR_ERROR), err);
}
static void export_enc_cb(lv_event_t *e) { (void)e; ui_prompt(tr(STR_BACKUP_PASSWORD), tr(STR_BACKUP_PW_NOTE), "", true, export_pw_cb, NULL); }
static void export_plain_yes(bool yes, void *ud)
{
    (void)ud; if (!yes) return;
    char path[96], err[96];
    if (backup_export(NULL, path, sizeof(path), err, sizeof(err))) ui_toastf(tr(STR_BACKUP_WRITTEN), path); else ui_alert(tr(STR_ERROR), err);
}
static void export_plain_cb(lv_event_t *e) { (void)e; ui_confirm(tr(STR_BACKUP_EXPORT_PLAIN), tr(STR_BACKUP_PLAIN_WARN), export_plain_yes, NULL); }
static void reboot_timer(lv_timer_t *t) { (void)t; esp_restart(); }
static void import_done(bool ok, const char *err)
{
    if (!ok) { ui_alert(tr(STR_ERROR), err); return; }
    ui_alert(tr(STR_SET_BACKUP), tr(STR_BACKUP_IMPORTED));
    lv_timer_t *t = lv_timer_create(reboot_timer, 2000, NULL); lv_timer_set_repeat_count(t, 1);
}
static void import_pw_cb(const char *pw, void *ud) { (void)ud; char err[96]; import_done(backup_import(pw, err, sizeof(err)), err); }
static void import_cb(lv_event_t *e)
{
    (void)e;
    bool enc;
    if (!backup_find(&enc)) { ui_toast(tr(STR_BACKUP_NOT_FOUND)); return; }
    if (enc) { ui_prompt(tr(STR_BACKUP_ENC_FOUND), tr(STR_BACKUP_PASSWORD), "", true, import_pw_cb, NULL); return; }
    char err[96]; import_done(backup_import(NULL, err, sizeof(err)), err);
}

lv_obj_t *ui_backup_create(void *arg)
{
    (void)arg;
    lv_obj_t *scr = ui_screen_base();
    ui_topbar(scr, tr(STR_SET_BACKUP), true);
    lv_obj_t *c = ui_content(scr);
    lv_obj_t *card = ui_card(c);
    ui_label(card, tr(STR_BACKUP_PW_NOTE), FONT_SMALL, UI_MUTED);
    lv_obj_t *row = ui_row(card);
    ui_button(row, tr(STR_BACKUP_EXPORT_ENC), export_enc_cb, NULL);
    ui_button_colored(row, tr(STR_BACKUP_EXPORT_PLAIN), UI_CARD_HI, export_plain_cb, NULL);
    ui_label(card, tr(STR_BACKUP_PLAIN_WARN), FONT_SMALL, UI_MUTED);
    lv_obj_t *card2 = ui_card(c);
    bool enc = false; bool found = backup_find(&enc);
    char b[160]; snprintf(b, sizeof(b), "%s: %s", tr(STR_BACKUP_IMPORT), found ? (enc ? BACKUP_ENC : BACKUP_PLAIN) : tr(STR_NONE));
    ui_label(card2, b, FONT_BODY, UI_TEXT);
    ui_button_colored(card2, tr(STR_BACKUP_IMPORT), UI_ACCENT2, import_cb, NULL);
    return scr;
}
