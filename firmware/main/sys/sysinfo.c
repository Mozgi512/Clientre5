#include <time.h>
#include "sysinfo.h"
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "driver/i2c_master.h"
#include "bsp/m5stack_tab5.h"
#include "settings/settings.h"
#include "i18n/i18n.h"

static const char *TAG = "sysinfo";
#define INA226_ADDR 0x41
#define RX8130_ADDR 0x32

static i2c_master_dev_handle_t s_ina = NULL, s_rtc = NULL;
static power_info_t s_pwr;
static bool s_charge_stat_valid = false;

static bool i2c_wr_rd(i2c_master_dev_handle_t dev, const uint8_t *w, size_t wl, uint8_t *r, size_t rl)
{
    if (!dev) return false;
    esp_err_t e = r ? i2c_master_transmit_receive(dev, w, wl, r, rl, 50) : i2c_master_transmit(dev, w, wl, 50);
    return e == ESP_OK;
}

static bool ina_read16(uint8_t reg, uint16_t *v)
{
    uint8_t r[2];
    if (!i2c_wr_rd(s_ina, &reg, 1, r, 2)) return false;
    *v = (r[0] << 8) | r[1];
    return true;
}

void sysinfo_init(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) { ESP_LOGW(TAG, "no I2C bus"); return; }
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = INA226_ADDR, .scl_speed_hz = 400000 };
    if (i2c_master_probe(bus, INA226_ADDR, 50) == ESP_OK && i2c_master_bus_add_device(bus, &cfg, &s_ina) == ESP_OK) {
        /* config: avg 16, 1.1ms conversions, continuous shunt+bus */
        uint8_t w[3] = { 0x00, 0x45, 0x27 };
        i2c_wr_rd(s_ina, w, 3, NULL, 0);
        /* calibration for 5mR shunt: CAL = 0.00512 / (LSB * R); LSB = 1mA -> 1024 */
        uint8_t c[3] = { 0x05, 0x04, 0x00 };
        i2c_wr_rd(s_ina, c, 3, NULL, 0);
        ESP_LOGI(TAG, "INA226 ready");
    } else {
        ESP_LOGW(TAG, "INA226 not found");
    }
    cfg.device_address = RX8130_ADDR;
    if (i2c_master_probe(bus, RX8130_ADDR, 50) == ESP_OK && i2c_master_bus_add_device(bus, &cfg, &s_rtc) == ESP_OK) {
        ESP_LOGI(TAG, "RX8130 ready");
    } else {
        ESP_LOGW(TAG, "RX8130 not found");
    }
    sysinfo_apply_timezone();
    sysinfo_time_from_rtc();
    sysinfo_poll();
}

bool sysinfo_ina226_present(void) { return s_ina != NULL; }
bool sysinfo_rtc_present(void) { return s_rtc != NULL; }

static int voltage_to_percent(float v)
{
    /* 2S Li-ion (NP-F550 style): 6.0V empty .. 8.4V full */
    float pct = (v - 6.4f) / (8.35f - 6.4f) * 100.0f;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (int)(pct + 0.5f);
}

void sysinfo_poll(void)
{
    if (!s_ina) { s_pwr.valid = false; s_pwr.percent = -1; s_pwr.src = PWR_UNKNOWN; return; }
    uint16_t bus, cur, pwr;
    if (!ina_read16(0x02, &bus) || !ina_read16(0x04, &cur) || !ina_read16(0x03, &pwr)) { s_pwr.valid = false; return; }
    s_pwr.bus_v = bus * 1.25f / 1000.0f;
    s_pwr.current_a = (int16_t)cur * 0.001f;
    s_pwr.power_w = pwr * 0.025f;
    s_pwr.percent = voltage_to_percent(s_pwr.bus_v);
    s_pwr.valid = true;
    /* Charge status from IO expander 2 pin 4 (input, active low) when available. */
    esp_io_expander_handle_t ex2 = bsp_io_expander1_init();
    bool charging = false;
    if (ex2) {
        uint32_t lv = 0;
        if (esp_io_expander_get_level(ex2, IO_EXPANDER_PIN_NUM_4, &lv) == ESP_OK) {
            charging = (lv == 0);
            s_charge_stat_valid = true;
        }
    }
    if (s_pwr.current_a < -0.05f || charging) s_pwr.src = PWR_USB_CHARGING;
    else if (s_pwr.current_a > 0.08f) s_pwr.src = PWR_BATTERY;
    else s_pwr.src = s_pwr.bus_v > 8.3f ? PWR_USB_STANDBY : PWR_USB;
}

power_info_t sysinfo_power(void) { return s_pwr; }
int sysinfo_battery_percent(void) { return s_pwr.valid ? s_pwr.percent : -1; }

const char *sysinfo_power_src_text(void)
{
    switch (s_pwr.src) {
    case PWR_USB: return tr(STR_PWR_USB_C);
    case PWR_USB_CHARGING: return tr(STR_PWR_USB_C_CHARGE);
    case PWR_USB_STANDBY: return tr(STR_PWR_USB_C_STANDBY);
    case PWR_BATTERY: return tr(STR_PWR_BATTERY);
    default: return tr(STR_UNKNOWN);
    }
}

/* newlib on ESP-IDF has no timegm(); days-from-civil algorithm. */
static time_t utc_mktime(const struct tm *t)
{
    int y = t->tm_year + 1900, m = t->tm_mon + 1, d = t->tm_mday;
    if (m <= 2) { y--; m += 12; }
    long days = 365L * y + y / 4 - y / 100 + y / 400 + (153L * (m - 3) + 2) / 5 + d - 719469L;
    return (time_t)days * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
}

static int bcd2i(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
static uint8_t i2bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

bool sysinfo_rtc_read(struct tm *out)
{
    uint8_t reg = 0x10, r[7];
    if (!i2c_wr_rd(s_rtc, &reg, 1, r, 7)) return false;
    memset(out, 0, sizeof(*out));
    out->tm_sec = bcd2i(r[0] & 0x7F);
    out->tm_min = bcd2i(r[1] & 0x7F);
    out->tm_hour = bcd2i(r[2] & 0x3F);
    out->tm_mday = bcd2i(r[4] & 0x3F);
    out->tm_mon = bcd2i(r[5] & 0x1F) - 1;
    out->tm_year = bcd2i(r[6]) + 100;
    return out->tm_year >= 124 && out->tm_mon >= 0 && out->tm_mon < 12 && out->tm_mday >= 1;
}

bool sysinfo_rtc_write(const struct tm *in)
{
    uint8_t w[8] = { 0x10, i2bcd(in->tm_sec), i2bcd(in->tm_min), i2bcd(in->tm_hour),
                     (uint8_t)(1 << in->tm_wday), i2bcd(in->tm_mday), i2bcd(in->tm_mon + 1), i2bcd(in->tm_year % 100) };
    return i2c_wr_rd(s_rtc, w, 8, NULL, 0);
}

void sysinfo_apply_timezone(void)
{
    /* POSIX TZ has the sign inverted: UTC+9 -> "UTC-9" */
    int off = g_settings.utc_offset_min;
    char tz[32];
    snprintf(tz, sizeof(tz), "UTC%c%d:%02d", off >= 0 ? '-' : '+', abs(off) / 60, abs(off) % 60);
    setenv("TZ", tz, 1);
    tzset();
}

void sysinfo_time_from_rtc(void)
{
    struct tm tm;
    if (!sysinfo_rtc_read(&tm)) return;
    /* RTC keeps UTC */
    time_t t = utc_mktime(&tm);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

static void sntp_sync_cb(struct timeval *tv)
{
    (void)tv;
    time_t now = time(NULL);
    struct tm tm; gmtime_r(&now, &tm);
    sysinfo_rtc_write(&tm);
    ESP_LOGI(TAG, "time synced from NTP, RTC updated");
}

void sysinfo_start_sntp(void)
{
    static bool started = false;
    if (started) { esp_netif_sntp_start(); return; }
    started = true;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = sntp_sync_cb;
    cfg.start = true;
    esp_netif_sntp_init(&cfg);
}

uint32_t sysinfo_uptime_s(void) { return (uint32_t)(esp_timer_get_time() / 1000000ULL); }
const char *sysinfo_app_version(void) { return esp_app_get_description()->version; }
const char *sysinfo_app_project(void) { return esp_app_get_description()->project_name; }

void sysinfo_mem(size_t *dram_free, size_t *dram_total, size_t *dma_free, size_t *dma_total,
                 size_t *psram_free, size_t *psram_total)
{
    if (dram_free) *dram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (dram_total) *dram_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (dma_free) *dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
    if (dma_total) *dma_total = heap_caps_get_total_size(MALLOC_CAP_DMA);
    if (psram_free) *psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (psram_total) *psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
}
