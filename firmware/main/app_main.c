#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/m5stack_tab5.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "settings/settings.h"
#include "i18n/i18n.h"
#include "fonts/fonts.h"
#include "ui/ui.h"
#include "ui/ui_boot.h"
#include "sys/sysinfo.h"
#include "net/wifi_mgr.h"
#include "storage/sd.h"
#include "input/keyboard.h"
#include "ime/ime.h"
#include "ime/ime_ui.h"
#include "servers/server_store.h"
#include "crypto/secure_store.h"
#include "sys/screenshot.h"
#include "sys/usb_mgr.h"
#include "media/audio_player.h"
#include "web/webfm.h"
#include "net/tailscale.h"

void ui_screensaver_arm(void);

static void wifi_global_cb(wifi_ev_t ev, void *user)
{
    (void)user;
    if (ev == WIFI_EV_CONNECTED) {
        tailscale_on_wifi(true);
        sysinfo_start_sntp();
        if (g_settings.webfm_autostart) webfm_start();
    }
}

static const char *TAG = "app";
static lv_display_t *s_display;
static int64_t s_boot_logo_visible_at;

#define BOOT_LOGO_MIN_VISIBLE_MS 1000

static bool display_init_early(void)
{
    int64_t display_started = esp_timer_get_time();
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = {
            .task_priority = 4,
            .task_stack = 16384,
            .task_affinity = 0,
            .task_max_sleep_ms = 500,
            .task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
            .timer_period_ms = 5,
        },
        /* Partial draw buffers (50 lines, double) in PSRAM: internal RAM is too
         * tight on this board once esp_hosted and libssh are in, and DMA2D can
         * read PSRAM on the P4. */
        .buffer_size = BSP_LCD_H_RES * 50,
        .double_buffer = true,
        .flags = { .buff_dma = false, .buff_spiram = true, .sw_rotate = true },
    };
    s_display = bsp_display_start_with_config(&cfg);
    if (!s_display) { ESP_LOGE(TAG, "display init failed"); return false; }
    /* Settings have deliberately not been loaded yet.  Use the board's normal
     * landscape orientation for the earliest possible boot logo, then apply
     * the saved orientation after NVS is ready. */
    bsp_display_lock(0);
    lv_display_set_rotation(s_display, LV_DISPLAY_ROTATION_90);
    int64_t logo_started = esp_timer_get_time();
    ui_boot_show();
    bsp_display_unlock();
    /* Draw before lighting the panel to avoid a blank flash. */
    bsp_display_brightness_set(80);
    s_boot_logo_visible_at = esp_timer_get_time();
    ESP_LOGI(TAG, "display ready in %lld ms; boot logo submitted in %lld ms",
             (esp_timer_get_time() - display_started) / 1000,
             (esp_timer_get_time() - logo_started) / 1000);
    return true;
}

static void display_apply_settings(void)
{
    bsp_display_lock(0);
    lv_display_set_rotation(s_display, (lv_display_rotation_t)(g_settings.rotation & 3));
    lv_refr_now(s_display);
    bsp_display_unlock();
    bsp_display_brightness_set(g_settings.brightness);
}

static void boot_logo_wait_minimum(void)
{
    int64_t elapsed_us = esp_timer_get_time() - s_boot_logo_visible_at;
    int64_t remaining_us = (int64_t)BOOT_LOGO_MIN_VISIBLE_MS * 1000 - elapsed_us;
    if (remaining_us > 0)
        vTaskDelay(pdMS_TO_TICKS((remaining_us + 999) / 1000));
}

static void idle_task(void *arg)
{
    (void)arg;
    for (;;) {
        sysinfo_poll();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    int64_t boot_started = esp_timer_get_time();
    ESP_LOGI(TAG, "Clientre 5 starting");
    esp_log_level_set("i2c.master", ESP_LOG_NONE);   /* keyboard/INA226 probes are expected to NACK when absent */
    /* Bring up only the hardware required by the panel.  Touch must be powered
     * before bsp_display_start_with_config(): the BSP identifies the Tab5 board
     * revision by probing that controller and asserts if it is still starting. */
    bsp_i2c_init();
    bsp_io_expander_init();
    bsp_io_expander1_init();
    bsp_feature_enable(BSP_FEATURE_LCD, true);
    bsp_feature_enable(BSP_FEATURE_TOUCH, true);
    /* Keep the board's proven rail-enable sequence.  These are only expander
     * writes; the Wi-Fi and USB software stacks still start after the logo. */
    bsp_feature_enable(BSP_FEATURE_WIFI, true);
    bsp_feature_enable(BSP_FEATURE_USB, true);
    if (!display_init_early()) return;

    /* Everything below runs while the already-rendered logo remains visible. */
    settings_init();
    display_apply_settings();
    secure_store_init();
    sysinfo_init();
    sd_mount();
    server_store_init();   /* may live on the card, so only after it is mounted */

    bsp_display_lock(0);
    fonts_init();
    ime_init();
    keyboard_init();
    bsp_display_unlock();

    wifi_mgr_set_global_cb(wifi_global_cb, NULL);
    wifi_mgr_init();
    usb_mgr_init();
    audio_player_init();
    webfm_init();
    tailscale_init();
    /* Services are initialized; connection establishment continues in their
     * background tasks. Keep the logo for one second from backlight-on, with
     * all initialization time above counting toward that interval. */
    boot_logo_wait_minimum();
    bsp_display_lock(0);
    ui_init();
    ime_ui_init();
    lv_display_trigger_activity(NULL);
    screenshot_init();
    ui_screensaver_arm();
    if (g_settings.lang == LANG_UNSET) {
        ui_nav_replace(ui_lang_create, NULL);
    } else {
        ui_nav_home();
    }
    bsp_display_unlock();
    xTaskCreate(idle_task, "idle_poll", 4096, NULL, 2, NULL);
    ESP_LOGI(TAG, "boot complete in %lld ms, free heap %lu",
             (esp_timer_get_time() - boot_started) / 1000, (unsigned long)esp_get_free_heap_size());
}
