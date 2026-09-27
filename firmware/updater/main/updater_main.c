/* tab5_factory_updater: tiny "UPLOAD" firmware living in the factory partition.
 * It reads /sdcard/update/request.json, verifies the package on the TF card and
 * flashes it into main_app, then boots back into the main application. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_format.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "mbedtls/sha256.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "updater";
#define REQ "/sdcard/update/request.json"

/* Tab5: TF card on SDMMC slot 0 (CLK 43, CMD 44, D0-3 39..42), 3.3 V. */
static sdmmc_card_t *s_card;
static bool mount_sd(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4; slot.clk = 43; slot.cmd = 44; slot.d0 = 39; slot.d1 = 40; slot.d2 = 41; slot.d3 = 42;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t mc = { .format_if_mount_failed = false, .max_files = 4, .allocation_unit_size = 16 * 1024 };
    esp_err_t e = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mc, &s_card);
    if (e != ESP_OK) ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(e));
    return e == ESP_OK;
}

static void boot_main_and_restart(void)
{
    const esp_partition_t *app = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "main_app");
    if (app) esp_ota_set_boot_partition(app);
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

static bool sha256_file(const char *path, char *hex)
{
    FILE *f = fopen(path, "rb"); if (!f) return false;
    mbedtls_sha256_context c; mbedtls_sha256_init(&c); mbedtls_sha256_starts(&c, 0);
    uint8_t *buf = malloc(16384); size_t n;
    while ((n = fread(buf, 1, 16384, f)) > 0) mbedtls_sha256_update(&c, buf, n);
    uint8_t d[32]; mbedtls_sha256_finish(&c, d); mbedtls_sha256_free(&c); free(buf); fclose(f);
    for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", d[i]);
    return true;
}

static bool flash_app(const char *path)
{
    const esp_partition_t *app = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "main_app");
    if (!app) { ESP_LOGE(TAG, "main_app partition missing"); return false; }
    FILE *f = fopen(path, "rb"); if (!f) { ESP_LOGE(TAG, "cannot open %s", path); return false; }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > (long)app->size) { ESP_LOGE(TAG, "bad size %ld", size); fclose(f); return false; }
    esp_ota_handle_t h;
    if (esp_ota_begin(app, size, &h) != ESP_OK) { ESP_LOGE(TAG, "ota begin failed"); fclose(f); return false; }
    uint8_t *buf = malloc(16384); size_t n, done = 0; int last = -1; bool ok = true;
    while ((n = fread(buf, 1, 16384, f)) > 0) {
        if (esp_ota_write(h, buf, n) != ESP_OK) { ok = false; break; }
        done += n; int pct = (int)((uint64_t)done * 100 / size);
        if (pct / 10 != last / 10) { ESP_LOGI(TAG, "flashing %d%%", pct); last = pct; }
    }
    free(buf); fclose(f);
    if (!ok || esp_ota_end(h) != ESP_OK) { ESP_LOGE(TAG, "ota write/end failed"); return false; }
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Tab5 factory updater starting");
    if (!mount_sd()) { boot_main_and_restart(); return; }
    FILE *f = fopen(REQ, "rb");
    if (!f) { ESP_LOGW(TAG, "no update request; booting main app"); esp_vfs_fat_sdcard_unmount("/sdcard", s_card); boot_main_and_restart(); return; }
    char buf[1024]; size_t n = fread(buf, 1, sizeof(buf) - 1, f); buf[n] = 0; fclose(f);
    cJSON *j = cJSON_Parse(buf);
    const char *file = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "file")) : NULL;
    const char *sha = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "sha256")) : NULL;
    bool ok = false;
    if (file) {
        char hex[65] = "";
        if (sha && sha[0]) { sha256_file(file, hex); if (strcasecmp(hex, sha) != 0) { ESP_LOGE(TAG, "SHA256 mismatch"); goto out; } }
        ESP_LOGI(TAG, "package verified, flashing %s", file);
        ok = flash_app(file);
    }
out:
    if (j) cJSON_Delete(j);
    unlink(REQ);   /* never loop on a bad request */
    if (ok) {
        ESP_LOGI(TAG, "update complete");
        char done[64]; snprintf(done, sizeof(done), "/sdcard/update/last_result.txt");
        FILE *r = fopen(done, "w"); if (r) { fprintf(r, "ok\n"); fclose(r); }
    } else {
        FILE *r = fopen("/sdcard/update/last_result.txt", "w"); if (r) { fprintf(r, "failed\n"); fclose(r); }
    }
    esp_vfs_fat_sdcard_unmount("/sdcard", s_card);
    boot_main_and_restart();
}
