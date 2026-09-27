#include "ota.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_format.h"
#include "esp_system.h"
#include "cJSON.h"
#include "storage/sd.h"
#include "crypto/secure_store.h"
#include "sys/sysinfo.h"

static const char *TAG = "ota";
#define UPDATER_PROJECT "tab5_factory_updater"

static esp_http_client_handle_t open_http(const char *url, int *status, int *len)
{
    esp_http_client_config_t cfg = { .url = url, .timeout_ms = 15000, .crt_bundle_attach = esp_crt_bundle_attach, .buffer_size = 4096, .buffer_size_tx = 2048, .user_agent = "Clientre5/1.0" };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return NULL;
    /* open/fetch_headers do not follow redirects by themselves, and GitHub release
     * and raw downloads always answer with one. Same loop as esp_https_ota. */
    for (int hop = 0; hop < 5; hop++) {
        if (esp_http_client_open(h, 0) != ESP_OK) { esp_http_client_cleanup(h); return NULL; }
        int cl = esp_http_client_fetch_headers(h);
        *status = esp_http_client_get_status_code(h);
        *len = cl;
        bool redirect = *status == 301 || *status == 302 || *status == 303 || *status == 307 || *status == 308;
        if (!redirect) return h;
        if (esp_http_client_set_redirection(h) != ESP_OK) break;
        char drain[256];
        while (esp_http_client_read(h, drain, sizeof(drain)) > 0) {}
    }
    return h;
}

bool ota_fetch_manifest(const char *url, ota_manifest_t *out, char *err, size_t err_cap)
{
    memset(out, 0, sizeof(*out));
    int status = 0, len = 0;
    esp_http_client_handle_t h = open_http(url, &status, &len);
    if (!h) { snprintf(err, err_cap, "HTTP client failed"); return false; }
    if (status != 200) { snprintf(err, err_cap, "HTTP %d", status); esp_http_client_close(h); esp_http_client_cleanup(h); return false; }
    char *buf = malloc(8192); int tot = 0, n;
    while ((n = esp_http_client_read(h, buf + tot, 8191 - tot)) > 0) { tot += n; if (tot >= 8191) break; }
    buf[tot] = 0;
    esp_http_client_close(h); esp_http_client_cleanup(h);
    cJSON *j = cJSON_Parse(buf); free(buf);
    if (!j) { snprintf(err, err_cap, "Bad update JSON"); return false; }
    const char *v;
#define GS(field, key, cap) if ((v = cJSON_GetStringValue(cJSON_GetObjectItem(j, key)))) strlcpy(out->field, v, cap)
    GS(version, "version", 32); GS(url, "url", 200); GS(sha256, "sha256", 65); GS(notes, "notes", 200); GS(project, "project", 48);
    cJSON *sz = cJSON_GetObjectItem(j, "size"); if (cJSON_IsNumber(sz)) out->size = (uint32_t)sz->valuedouble;
    cJSON *u = cJSON_GetObjectItem(j, "updater");
    if (u) {
        if ((v = cJSON_GetStringValue(cJSON_GetObjectItem(u, "version")))) strlcpy(out->upd_version, v, 32);
        if ((v = cJSON_GetStringValue(cJSON_GetObjectItem(u, "url")))) strlcpy(out->upd_url, v, 200);
        if ((v = cJSON_GetStringValue(cJSON_GetObjectItem(u, "sha256")))) strlcpy(out->upd_sha256, v, 65);
        sz = cJSON_GetObjectItem(u, "size"); if (cJSON_IsNumber(sz)) out->upd_size = (uint32_t)sz->valuedouble;
    }
#undef GS
    cJSON_Delete(j);
    if (!out->version[0] || !out->url[0]) { snprintf(err, err_cap, "Version fields missing"); return false; }
    out->valid = true;
    return true;
}

static bool sha_matches(const char *path, const char *hex)
{
    if (!hex || !hex[0]) return true;
    uint8_t d[32]; char h[65];
    if (!secure_sha256_file(path, d, NULL, NULL)) return false;
    secure_hex(d, 32, h);
    return strcasecmp(h, hex) == 0;
}

bool ota_download(const char *url, const char *path, const char *sha256, uint32_t expect_size, ota_progress_cb_t cb, void *user, char *err, size_t err_cap)
{
    if (!sd_is_mounted()) { snprintf(err, err_cap, "TF card not mounted"); return false; }
    uint64_t tot, fr;
    if (sd_space(SD_MOUNT, &tot, &fr) && fr < 20ull * 1024 * 1024) { snprintf(err, err_cap, "TF card has less than 20MB free"); return false; }
    fs_mkdir_p(SD_MOUNT_OTA);
    int status = 0, len = 0;
    esp_http_client_handle_t h = open_http(url, &status, &len);
    if (!h) { snprintf(err, err_cap, "HTTP client failed"); return false; }
    if (status != 200) { snprintf(err, err_cap, "HTTP %d", status); esp_http_client_close(h); esp_http_client_cleanup(h); return false; }
    char tmp[320]; snprintf(tmp, sizeof(tmp), "%s.part", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { snprintf(err, err_cap, "Cannot write firmware to SD"); esp_http_client_close(h); esp_http_client_cleanup(h); return false; }
    size_t bs = 16384; uint8_t *buf = malloc(bs); uint32_t done = 0; int n; bool ok = true; int stalls = 0;
    uint32_t total = len > 0 ? (uint32_t)len : expect_size;
    while (1) {
        n = esp_http_client_read(h, (char *)buf, bs);
        if (n < 0) { snprintf(err, err_cap, "Transfer failed"); ok = false; break; }
        if (n == 0) { if (esp_http_client_is_complete_data_received(h) || (total && done >= total)) break; if (++stalls > 200) { snprintf(err, err_cap, "Download stalled"); ok = false; break; } continue; }
        stalls = 0;
        if (fwrite(buf, 1, n, f) != (size_t)n) { snprintf(err, err_cap, "Cannot write firmware to SD"); ok = false; break; }
        done += n;
        if (cb && total) cb((int)((uint64_t)done * 100 / total), NULL, user);
        if (expect_size && done > expect_size + 1024) { snprintf(err, err_cap, "Download exceeds size"); ok = false; break; }
    }
    free(buf); fclose(f);
    esp_http_client_close(h); esp_http_client_cleanup(h);
    if (ok && expect_size && done != expect_size) { snprintf(err, err_cap, "Size mismatch"); ok = false; }
    if (ok && !sha_matches(tmp, sha256)) { snprintf(err, err_cap, "Package SHA256 failed"); ok = false; }
    if (!ok) { unlink(tmp); return false; }
    unlink(path);
    if (rename(tmp, path) != 0) { snprintf(err, err_cap, "Cannot write firmware to SD"); return false; }
    return true;
}

static bool read_app_desc_file(const char *path, esp_app_desc_t *desc)
{
    FILE *f = fopen(path, "rb"); if (!f) return false;
    /* image header (24) + segment header (8) + app desc */
    uint8_t hdr[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)];
    bool ok = fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr) && hdr[0] == ESP_IMAGE_HEADER_MAGIC && fread(desc, 1, sizeof(*desc), f) == sizeof(*desc) && desc->magic_word == ESP_APP_DESC_MAGIC_WORD;
    fclose(f);
    return ok;
}

bool ota_package_ready(const char *sha256, char *ver_out, size_t cap)
{
    if (!fs_exists(OTA_APP_FILE)) return false;
    esp_app_desc_t d;
    if (!read_app_desc_file(OTA_APP_FILE, &d)) return false;
    if (strcmp(d.project_name, sysinfo_app_project()) != 0) return false;
    if (sha256 && sha256[0] && !sha_matches(OTA_APP_FILE, sha256)) return false;
    if (ver_out) strlcpy(ver_out, d.version, cap);
    return true;
}

bool ota_updater_present(char *ver, size_t cap)
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "factory");
    if (!p) return false;
    esp_app_desc_t d;
    if (esp_ota_get_partition_description(p, &d) != ESP_OK) return false;
    if (strcmp(d.project_name, UPDATER_PROJECT) != 0) return false;
    if (ver) strlcpy(ver, d.version, cap);
    return true;
}

bool ota_booted_by_launcher(void) { return !ota_updater_present(NULL, 0); }

bool ota_install_via_updater(const char *sha256, char *err, size_t err_cap)
{
    if (ota_booted_by_launcher()) { snprintf(err, err_cap, "Updater missing (Launcher mode)"); return false; }
    char ver[32];
    if (!ota_package_ready(sha256, ver, sizeof(ver))) { snprintf(err, err_cap, "Package verification failed"); return false; }
    uint8_t d[32]; char hex[65];
    secure_sha256_file(OTA_APP_FILE, d, NULL, NULL); secure_hex(d, 32, hex);
    FILE *f = fopen(OTA_APP_FILE, "rb"); fseek(f, 0, SEEK_END); long size = ftell(f); fclose(f);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "file", OTA_APP_FILE);
    cJSON_AddStringToObject(j, "sha256", hex);
    cJSON_AddNumberToObject(j, "size", (double)size);
    cJSON_AddStringToObject(j, "version", ver);
    cJSON_AddStringToObject(j, "target", "main_app");
    char *s = cJSON_PrintUnformatted(j); cJSON_Delete(j);
    bool ok = fs_write_all(OTA_REQ_FILE, s, strlen(s)); free(s);
    if (!ok) { snprintf(err, err_cap, "Update request write failed"); return false; }
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "factory");
    if (esp_ota_set_boot_partition(p) != ESP_OK) { snprintf(err, err_cap, "Switch updater failed"); return false; }
    ESP_LOGI(TAG, "rebooting into updater");
    sd_unmount();
    esp_restart();
    return true;
}

bool ota_flash_updater(const char *path, char *err, size_t err_cap)
{
    esp_app_desc_t d;
    if (!read_app_desc_file(path, &d) || strcmp(d.project_name, UPDATER_PROJECT) != 0) { snprintf(err, err_cap, "Wrong server firmware project"); return false; }
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "factory");
    if (!p) { snprintf(err, err_cap, "No factory partition"); return false; }
    FILE *f = fopen(path, "rb"); if (!f) { snprintf(err, err_cap, "Cannot open package"); return false; }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > (long)p->size) { fclose(f); snprintf(err, err_cap, "Package too large"); return false; }
    if (esp_partition_erase_range(p, 0, p->size) != ESP_OK) { fclose(f); snprintf(err, err_cap, "Erase failed"); return false; }
    uint8_t *buf = malloc(4096); size_t off = 0, n; bool ok = true;
    while ((n = fread(buf, 1, 4096, f)) > 0) { if (esp_partition_write(p, off, buf, n) != ESP_OK) { ok = false; break; } off += n; }
    free(buf); fclose(f);
    if (!ok) { snprintf(err, err_cap, "Write failed"); return false; }
    return true;
}

int ota_compare_versions(const char *a, const char *b)
{
    while (*a == 'v' || *a == 'V') a++; while (*b == 'v' || *b == 'V') b++;
    for (int i = 0; i < 4; i++) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++; if (*b == '.') b++;
        if (!*a && !*b) break;
    }
    return 0;
}
