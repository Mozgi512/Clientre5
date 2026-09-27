#include "ui.h"
#include "ota/ota.h"
#include "sys/sysinfo.h"
#include "net/wifi_mgr.h"
#include "settings/settings.h"
#include "storage/sd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static ota_manifest_t s_man;
static lv_obj_t *s_scr, *s_cur, *s_latest, *s_notes, *s_state, *s_bar, *s_btn_dl, *s_btn_inst, *s_btn_upd;
typedef struct { volatile bool done; bool ok; int pct; char err[128]; int kind; } job_t;
static job_t s_job;

static void refresh(void)
{
    if (!s_scr) return;
    char b[160];
    snprintf(b, sizeof(b), tr(STR_OTA_CURRENT), sysinfo_app_version()); lv_label_set_text(s_cur, b);
    snprintf(b, sizeof(b), tr(STR_OTA_LATEST), s_man.valid ? s_man.version : "--"); lv_label_set_text(s_latest, b);
    lv_label_set_text(s_notes, s_man.valid ? s_man.notes : "");
    char ver[32];
    bool ready = ota_package_ready(s_man.valid ? s_man.sha256 : NULL, ver, sizeof(ver));
    bool launcher = ota_booted_by_launcher();
    if (launcher) lv_label_set_text(s_state, tr(STR_OTA_LAUNCHER_MODE));
    else if (ready) { snprintf(b, sizeof(b), "%s (%s)", tr(STR_OTA_SD_PACKAGE), ver); lv_label_set_text(s_state, b); }
    else if (s_man.valid && ota_compare_versions(sysinfo_app_version(), s_man.version) < 0) lv_label_set_text(s_state, tr(STR_OTA_FOUND));
    else if (s_man.valid) lv_label_set_text(s_state, tr(STR_OTA_LATEST_ALREADY));
    else lv_label_set_text(s_state, "");
    if (s_man.valid && s_man.url[0]) lv_obj_remove_flag(s_btn_dl, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s_btn_dl, LV_OBJ_FLAG_HIDDEN);
    if (ready && !launcher) lv_obj_remove_flag(s_btn_inst, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s_btn_inst, LV_OBJ_FLAG_HIDDEN);
    char uv[32] = "-"; ota_updater_present(uv, sizeof(uv));
    snprintf(b, sizeof(b), "%s: %s%s%s", tr(STR_OTA_UPDATER), uv, s_man.upd_version[0] ? " -> " : "", s_man.upd_version);
    lv_label_set_text(lv_obj_get_user_data(s_btn_upd), b);
    if (s_man.upd_url[0] && !launcher) lv_obj_remove_flag(s_btn_upd, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s_btn_upd, LV_OBJ_FLAG_HIDDEN);
}

static void progress_cb(int pct, const char *msg, void *user) { (void)msg; (void)user; s_job.pct = pct; }
static void job_task(void *arg)
{
    (void)arg;
    if (s_job.kind == 0) s_job.ok = ota_fetch_manifest(g_settings.ota_url, &s_man, s_job.err, sizeof(s_job.err));
    else if (s_job.kind == 1) s_job.ok = ota_download(s_man.url, OTA_APP_FILE, s_man.sha256, s_man.size, progress_cb, NULL, s_job.err, sizeof(s_job.err));
    else if (s_job.kind == 2) {
        char p[96] = SD_MOUNT_OTA "/tab5_factory_updater.bin";
        s_job.ok = ota_download(s_man.upd_url, p, s_man.upd_sha256, s_man.upd_size, progress_cb, NULL, s_job.err, sizeof(s_job.err));
        if (s_job.ok) s_job.ok = ota_flash_updater(p, s_job.err, sizeof(s_job.err));
    }
    s_job.done = true;
    vTaskDelete(NULL);
}
static void poll_cb(lv_timer_t *t)
{
    lv_obj_t *busy = lv_timer_get_user_data(t);
    if (!s_job.done) {
        if (s_job.kind >= 1 && busy) { char b[64]; snprintf(b, sizeof(b), tr(STR_OTA_DOWNLOADING), s_job.pct); lv_label_set_text(lv_obj_get_user_data(busy), b); }
        return;
    }
    lv_timer_delete(t);
    ui_busy_close(busy);
    if (!s_job.ok) ui_alert(tr(STR_ERROR), s_job.err);
    else if (s_job.kind == 1) ui_toast(tr(STR_OTA_DOWNLOADED));
    else if (s_job.kind == 2) ui_toast(tr(STR_OTA_UPDATER_DONE));
    refresh();
}
static void run_job(int kind, const char *busy_text)
{
    if (!wifi_mgr_is_connected()) { ui_toast(tr(STR_WIFI_NOT_CONNECTED)); return; }
    memset(&s_job, 0, sizeof(s_job)); s_job.kind = kind;
    lv_obj_t *busy = ui_busy(busy_text);
    xTaskCreate(job_task, "ota", 12288, NULL, 3, NULL);
    lv_timer_create(poll_cb, 250, busy);
}
static void check_cb(lv_event_t *e) { (void)e; run_job(0, tr(STR_OTA_CHECKING)); }
static void dl_cb(lv_event_t *e) { (void)e; run_job(1, tr(STR_OTA_DOWNLOAD)); }
static void upd_cb(lv_event_t *e) { (void)e; run_job(2, tr(STR_OTA_UPDATE_UPDATER)); }
static void inst_yes(bool yes, void *ud)
{
    (void)ud; if (!yes) return;
    char err[128];
    if (!ota_install_via_updater(s_man.valid ? s_man.sha256 : NULL, err, sizeof(err))) ui_alert(tr(STR_ERROR), err);
}
static void inst_cb(lv_event_t *e) { (void)e; ui_confirm(tr(STR_OTA_INSTALL), tr(STR_OTA_INSTALL_NOTE), inst_yes, NULL); }
static void del_cb(lv_event_t *e) { (void)e; s_scr = NULL; }

lv_obj_t *ui_ota_create(void *arg)
{
    (void)arg;
    s_scr = ui_screen_base();
    lv_obj_add_event_cb(s_scr, del_cb, LV_EVENT_DELETE, NULL);
    ui_topbar(s_scr, tr(STR_OTA_TITLE), true);
    lv_obj_t *c = ui_content(s_scr);
    lv_obj_t *card = ui_card(c);
    s_cur = ui_label(card, "", FONT_BODY, UI_TEXT);
    s_latest = ui_label(card, "", FONT_BODY, UI_TEXT);
    s_notes = ui_label(card, "", FONT_SMALL, UI_MUTED);
    s_state = ui_label(card, "", FONT_BODY, UI_ACCENT2);
    s_bar = NULL;
    lv_obj_t *row = ui_row(card);
    ui_button(row, tr(STR_OTA_CHECK), check_cb, NULL);
    s_btn_dl = ui_button_colored(row, tr(STR_OTA_DOWNLOAD), UI_CARD_HI, dl_cb, NULL);
    s_btn_inst = ui_button_colored(row, tr(STR_OTA_INSTALL), UI_ACCENT2, inst_cb, NULL);
    ui_label(c, tr(STR_OTA_INSTALL_NOTE), FONT_SMALL, UI_MUTED);
    lv_obj_t *ucard = ui_card(c);
    lv_obj_t *ul = ui_label(ucard, "", FONT_BODY, UI_TEXT);
    s_btn_upd = ui_button_colored(ucard, tr(STR_OTA_UPDATE_UPDATER), UI_CARD_HI, upd_cb, NULL);
    lv_obj_set_user_data(s_btn_upd, ul);
    ui_label(c, g_settings.ota_url, FONT_SMALL, UI_MUTED);
    refresh();
    return s_scr;
}
