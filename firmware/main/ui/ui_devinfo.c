#include "ui.h"
#include "sys/sysinfo.h"
#include "net/wifi_mgr.h"
#include "storage/sd.h"
#include "input/keyboard.h"
#include "sys/usb_mgr.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_flash.h"
#include <stdio.h>

static lv_obj_t *s_lbl;
static void fill(void)
{
    if (!s_lbl) return;
    char buf[1600]; int n = 0;
    esp_chip_info_t ci; esp_chip_info(&ci);
    n += snprintf(buf + n, sizeof(buf) - n, "%s: ESP32-P4 rev %d.%d, %d cores, 360 MHz\n", tr(STR_DEV_CHIP), ci.revision / 100, ci.revision % 100, ci.cores);
    n += snprintf(buf + n, sizeof(buf) - n, "%s: %s\n%s: %s (%s)\n", tr(STR_DEV_IDF), esp_get_idf_version(), tr(STR_DEV_APP_VER), sysinfo_app_version(), tr(STR_APP_TITLE));
    uint32_t fsz = 0; esp_flash_get_size(NULL, &fsz);
    n += snprintf(buf + n, sizeof(buf) - n, "%s: %lu MB\n", tr(STR_DEV_FLASH), (unsigned long)(fsz >> 20));
    char up[32]; ui_format_duration(up, sizeof(up), sysinfo_uptime_s());
    n += snprintf(buf + n, sizeof(buf) - n, "%s: %s\n", tr(STR_DEV_UPTIME), up);
    size_t df, dt, mf, mt, pf, pt; sysinfo_mem(&df, &dt, &mf, &mt, &pf, &pt);
    n += snprintf(buf + n, sizeof(buf) - n, "%s: %u / %u KB\n%s: %u / %u KB\n%s: %u / %u KB\n", tr(STR_DEV_HEAP), (unsigned)(df >> 10), (unsigned)(dt >> 10),
                  tr(STR_DEV_DMA), (unsigned)(mf >> 10), (unsigned)(mt >> 10), tr(STR_DEV_PSRAM), (unsigned)(pf >> 10), (unsigned)(pt >> 10));
    char sd[96]; sd_card_info(sd, sizeof(sd));
    n += snprintf(buf + n, sizeof(buf) - n, "%s: %s\n", tr(STR_DEV_TF), sd_is_mounted() ? sd : tr(STR_TF_NOT_MOUNTED));
    power_info_t p = sysinfo_power();
    if (p.valid) n += snprintf(buf + n, sizeof(buf) - n, "%s: %.2f V  %.2f A  %.1f W  (%d%%)\n%s: %s\n", tr(STR_DEV_BATTERY), p.bus_v, p.current_a, p.power_w, p.percent, tr(STR_DEV_POWER_SRC), sysinfo_power_src_text());
    else n += snprintf(buf + n, sizeof(buf) - n, "%s: INA226 -\n", tr(STR_DEV_BATTERY));
    char ip4[48], ip6[64]; wifi_mgr_get_ip4(ip4, sizeof(ip4)); wifi_mgr_get_ip6(ip6, sizeof(ip6));
    n += snprintf(buf + n, sizeof(buf) - n, "WiFi: %s  %s\nIPv6: %s\n", wifi_mgr_is_connected() ? wifi_mgr_ssid() : tr(STR_WIFI_NOT_CONNECTED), ip4, ip6[0] ? ip6 : "-");
    n += snprintf(buf + n, sizeof(buf) - n, "%s: %s%s\nUSB: %s\n", tr(STR_KEYBOARD), keyboard_present() ? "Tab5 I2C" : (keyboard_usb_present() ? "USB HID" : tr(STR_ABSENT)),
                  keyboard_present() ? "" : "", usb_mgr_status_text());
    n += snprintf(buf + n, sizeof(buf) - n, "\n%s:\n- Display: M5Stack Tab5 720x1280 MIPI-DSI (ILI9881C / ST7123)\n- Touch: GT911 / ST7123\n- Audio: ES8388 + ES7210\n- WiFi: ESP32-C6 (SDIO)\n- Power: INA226, RTC: RX8130CE%s%s\n- IMU: BMI270\n- IO expanders: PI4IOE5V6408 x2",
                  tr(STR_DEV_PERIPHERALS), sysinfo_ina226_present() ? " (ok)" : " (INA226 n/a)", sysinfo_rtc_present() ? "" : " (RTC n/a)");
    lv_label_set_text(s_lbl, buf);
}
static void timer_cb(lv_timer_t *t) { (void)t; fill(); }
static void del_cb(lv_event_t *e) { (void)e; s_lbl = NULL; }

lv_obj_t *ui_devinfo_create(void *arg)
{
    (void)arg;
    lv_obj_t *scr = ui_screen_base();
    lv_obj_add_event_cb(scr, del_cb, LV_EVENT_DELETE, NULL);
    ui_topbar(scr, tr(STR_DEVINFO), true);
    lv_obj_t *c = ui_content(scr);
    lv_obj_t *card = ui_card(c);
    s_lbl = ui_label(card, "", FONT_BODY, UI_TEXT);
    fill();
    ui_timer_bind(lv_timer_create(timer_cb, 2000, NULL), scr);
    return scr;
}
