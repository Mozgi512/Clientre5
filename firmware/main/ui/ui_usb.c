#include "ui.h"
#include "sys/usb_mgr.h"
#include "storage/sd.h"
#include <stdio.h>

static lv_obj_t *s_status, *s_scr;
static void refresh(void)
{
    if (!s_scr) return;
    char b[300];
    snprintf(b, sizeof(b), "%s\n%s: %s\n%s: %s", usb_mgr_status_text(), tr(STR_USB_DISK_MODE), usb_mgr_disk_mode_active() ? tr(STR_ON) : tr(STR_OFF),
             tr(STR_USB_HOST), usb_mgr_msc_mounted() ? tr(STR_USB_MOUNTED) : (usb_mgr_msc_present() ? tr(STR_USB_WAITING) : tr(STR_USB_NOT_MOUNTED)));
    lv_label_set_text(s_status, b);
}
static void disk_start_cb(lv_event_t *e)
{
    (void)e;
    if (!sd_is_mounted()) { ui_toast(tr(STR_TF_NOT_MOUNTED)); return; }
    lv_obj_t *b = ui_busy(tr(STR_PLEASE_WAIT)); lv_refr_now(NULL);
    bool ok = usb_mgr_start_disk_mode();
    ui_busy_close(b);
    ui_toast(ok ? tr(STR_USB_DISK_ON) : tr(STR_FAILED));
    refresh();
}
static void disk_stop_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *b = ui_busy(tr(STR_PLEASE_WAIT)); lv_refr_now(NULL);
    bool ok = usb_mgr_stop_disk_mode();
    ui_busy_close(b);
    ui_toast(ok ? tr(STR_USB_DISK_OFF) : tr(STR_FAILED));
    refresh();
}
static void browse_cb(lv_event_t *e) { (void)e; if (usb_mgr_msc_mounted()) ui_nav_push(ui_files_create, (void *)USB_MOUNT); else ui_toast(tr(STR_USB_NOT_MOUNTED)); }
static void eject_cb(lv_event_t *e) { (void)e; ui_toast(usb_mgr_eject_msc() ? tr(STR_DONE) : tr(STR_USB_NOT_MOUNTED)); refresh(); }
static void fmt_yes(bool yes, void *ud)
{
    (void)ud; if (!yes) return;
    lv_obj_t *b = ui_busy(tr(STR_PLEASE_WAIT)); lv_refr_now(NULL);
    bool ok = usb_mgr_format_msc();
    ui_busy_close(b);
    ui_toast(ok ? tr(STR_USB_FORMATTED) : tr(STR_FAILED));
    refresh();
}
static void fmt_cb(lv_event_t *e) { (void)e; if (!usb_mgr_msc_present()) { ui_toast(tr(STR_USB_NOT_MOUNTED)); return; } ui_confirm(tr(STR_USB_FORMAT), tr(STR_USB_FORMAT_WARN), fmt_yes, NULL); }
static void tick(lv_timer_t *t) { (void)t; refresh(); }
static void del_cb(lv_event_t *e) { (void)e; s_scr = NULL; s_status = NULL; }

lv_obj_t *ui_usb_create(void *arg)
{
    (void)arg;
    s_scr = ui_screen_base();
    lv_obj_add_event_cb(s_scr, del_cb, LV_EVENT_DELETE, NULL);
    ui_topbar(s_scr, tr(STR_USB_TITLE), true);
    lv_obj_t *c = ui_content(s_scr);
    lv_obj_t *card = ui_card(c);
    s_status = ui_label(card, "", FONT_BODY, UI_TEXT);
    ui_section_title(c, tr(STR_USB_DISK_MODE));
    lv_obj_t *card2 = ui_card(c);
    ui_label(card2, tr(STR_USB_DISK_NOTE), FONT_SMALL, UI_MUTED);
    lv_obj_t *row = ui_row(card2);
    ui_button(row, tr(STR_USB_DISK_START), disk_start_cb, NULL);
    ui_button_colored(row, tr(STR_USB_DISK_STOP), UI_CARD_HI, disk_stop_cb, NULL);
    ui_section_title(c, tr(STR_USB_HOST));
    lv_obj_t *card3 = ui_card(c);
    lv_obj_t *row2 = ui_row(card3);
    ui_button(row2, tr(STR_FILES), browse_cb, NULL);
    ui_button_colored(row2, tr(STR_USB_EJECT), UI_CARD_HI, eject_cb, NULL);
    ui_button_colored(row2, tr(STR_USB_FORMAT), UI_DANGER, fmt_cb, NULL);
    refresh();
    ui_timer_bind(lv_timer_create(tick, 1500, NULL), s_scr);
    return s_scr;
}
