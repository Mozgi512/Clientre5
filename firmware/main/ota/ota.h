#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define OTA_DIR SD_MOUNT_OTA
#define SD_MOUNT_OTA "/sdcard/update"
#define OTA_APP_FILE  "/sdcard/update/clientre5.bin"
#define OTA_REQ_FILE  "/sdcard/update/request.json"

typedef struct {
    bool valid;
    char version[32], url[200], sha256[65], notes[200], project[48];
    uint32_t size;
    char upd_version[32], upd_url[200], upd_sha256[65];
    uint32_t upd_size;
} ota_manifest_t;

typedef void (*ota_progress_cb_t)(int percent, const char *msg, void *user);

bool ota_fetch_manifest(const char *url, ota_manifest_t *out, char *err, size_t err_cap);
/* Downloads url to path, verifies sha256 (hex, may be empty). Blocking. */
bool ota_download(const char *url, const char *path, const char *sha256, uint32_t expect_size, ota_progress_cb_t cb, void *user, char *err, size_t err_cap);
/* Package on SD ready to install? (verified against manifest sha if given) */
bool ota_package_ready(const char *sha256, char *ver_out, size_t cap);
/* Write request.json, switch boot partition to the factory updater, reboot. */
bool ota_install_via_updater(const char *sha256, char *err, size_t err_cap);
/* Flash the updater (factory partition) from a downloaded file. */
bool ota_flash_updater(const char *path, char *err, size_t err_cap);
bool ota_updater_present(char *ver, size_t cap);    /* factory partition holds our updater */
bool ota_booted_by_launcher(void);
int  ota_compare_versions(const char *a, const char *b);   /* <0 if a<b */
#ifdef __cplusplus
}
#endif
