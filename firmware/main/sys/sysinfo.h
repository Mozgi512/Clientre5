#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { PWR_UNKNOWN = 0, PWR_USB, PWR_USB_CHARGING, PWR_USB_STANDBY, PWR_BATTERY } power_src_t;

typedef struct {
    float bus_v;        /* battery/system bus voltage */
    float current_a;    /* + = discharging, - = charging */
    float power_w;
    int percent;        /* -1 if unknown */
    power_src_t src;
    bool valid;
} power_info_t;

void sysinfo_init(void);                     /* INA226 + RX8130 on the BSP I2C bus */
void sysinfo_poll(void);                     /* refresh cached power info (~1 Hz) */
power_info_t sysinfo_power(void);
int sysinfo_battery_percent(void);           /* -1 if unknown */
const char *sysinfo_power_src_text(void);
bool sysinfo_rtc_read(struct tm *out);
bool sysinfo_rtc_write(const struct tm *in);
void sysinfo_apply_timezone(void);           /* from g_settings.utc_offset_min */
void sysinfo_time_from_rtc(void);
void sysinfo_start_sntp(void);
bool sysinfo_ina226_present(void);
bool sysinfo_rtc_present(void);
uint32_t sysinfo_uptime_s(void);
const char *sysinfo_app_version(void);
const char *sysinfo_app_project(void);
void sysinfo_mem(size_t *dram_free, size_t *dram_total, size_t *dma_free, size_t *dma_total,
                 size_t *psram_free, size_t *psram_total);

#ifdef __cplusplus
}
#endif
