#include "sd.h"
#include "backup.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cJSON.h"
#include "esp_log.h"
#include "settings/settings.h"
#include "servers/server_store.h"
#include "net/wifi_mgr.h"
#include "crypto/secure_store.h"
#include "mbedtls/base64.h"

static const char *TAG = "backup";

static char *build_json(bool plain)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device", "Clientre 5");
    cJSON_AddNumberToObject(root, "format", 1);
    char *srv = server_store_export_json(plain);
    char *wifi = wifi_mgr_export_json();
    cJSON_AddItemToObject(root, "servers", cJSON_Parse(srv));
    cJSON_AddItemToObject(root, "wifi", cJSON_Parse(wifi));
    free(srv); free(wifi);
    size_t olen = 0;
    mbedtls_base64_encode(NULL, 0, &olen, (const unsigned char *)&g_settings, sizeof(g_settings));
    char *b64 = malloc(olen + 1);
    mbedtls_base64_encode((unsigned char *)b64, olen + 1, &olen, (const unsigned char *)&g_settings, sizeof(g_settings));
    b64[olen] = 0;
    cJSON_AddStringToObject(root, "settings", b64);
    free(b64);
    char *s = cJSON_Print(root);
    cJSON_Delete(root);
    return s;
}

bool backup_export(const char *password, char *out_path, size_t cap, char *err, size_t err_cap)
{
    if (!sd_is_mounted()) { snprintf(err, err_cap, "TF card not mounted"); return false; }
    fs_mkdir_p(BACKUP_DIR);
    char *json = build_json(password == NULL);
    if (!json) { snprintf(err, err_cap, "Create backup JSON failed"); return false; }
    bool ok;
    if (password) {
        uint8_t *enc = NULL; size_t enc_len = 0;
        ok = secure_encrypt_pw(password, (const uint8_t *)json, strlen(json), &enc, &enc_len) && fs_write_all(BACKUP_ENC, enc, enc_len);
        free(enc);
        if (out_path) strlcpy(out_path, BACKUP_ENC, cap);
    } else {
        ok = fs_write_all(BACKUP_PLAIN, json, strlen(json));
        if (out_path) strlcpy(out_path, BACKUP_PLAIN, cap);
    }
    free(json);
    if (!ok) snprintf(err, err_cap, "Backup write failed");
    return ok;
}

bool backup_find(bool *encrypted)
{
    if (fs_exists(BACKUP_ENC)) { if (encrypted) *encrypted = true; return true; }
    if (fs_exists(BACKUP_PLAIN)) { if (encrypted) *encrypted = false; return true; }
    return false;
}

static bool apply_json(const char *json, char *err, size_t err_cap)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) { snprintf(err, err_cap, "Bad backup JSON"); return false; }
    cJSON *srv = cJSON_GetObjectItem(root, "servers");
    cJSON *wifi = cJSON_GetObjectItem(root, "wifi");
    if (!srv && !wifi) { cJSON_Delete(root); snprintf(err, err_cap, "Backup missing namespaces"); return false; }
    if (srv) { char *s = cJSON_PrintUnformatted(srv); server_store_import_json(s); free(s); }
    if (wifi) { char *s = cJSON_PrintUnformatted(wifi); wifi_mgr_import_json(s); free(s); }
    const char *b64 = cJSON_GetStringValue(cJSON_GetObjectItem(root, "settings"));
    if (b64) {
        app_settings_t tmp; size_t n = 0;
        if (mbedtls_base64_decode((unsigned char *)&tmp, sizeof(tmp), &n, (const unsigned char *)b64, strlen(b64)) == 0 && n == sizeof(tmp)) {
            lang_t keep = g_settings.lang;
            g_settings = tmp;
            if (g_settings.lang == LANG_UNSET) g_settings.lang = keep;
            settings_save();
        }
    }
    cJSON_Delete(root);
    return true;
}

bool backup_import(const char *password, char *err, size_t err_cap)
{
    bool enc = false;
    if (!backup_find(&enc)) { snprintf(err, err_cap, "No backup found on SD."); return false; }
    char *data = NULL; size_t len = 0;
    if (!fs_read_all(enc ? BACKUP_ENC : BACKUP_PLAIN, &data, &len, 2 * 1024 * 1024)) { snprintf(err, err_cap, "Cannot read file."); return false; }
    bool ok;
    if (enc) {
        if (!password) { free(data); snprintf(err, err_cap, "Encrypted backup. Enter password."); return false; }
        uint8_t *plain = NULL; size_t plen = 0;
        if (!secure_decrypt_pw(password, (const uint8_t *)data, len, &plain, &plen)) { free(data); snprintf(err, err_cap, "Wrong password or damaged data"); return false; }
        ok = apply_json((const char *)plain, err, err_cap);
        free(plain);
    } else ok = apply_json(data, err, err_cap);
    free(data);
    ESP_LOGI(TAG, "import %s", ok ? "ok" : "failed");
    return ok;
}
