#include "server_store.h"
#include <string.h>
#include <stdlib.h>
#include "cJSON.h"
#include "esp_log.h"
#include "settings/settings.h"
#include "crypto/secure_store.h"
#include "storage/sd.h"
#include <unistd.h>

static const char *TAG = "servers";
static server_t s_list[SERVER_MAX];
static int s_n = 0;

/* With "credentials on the TF card" the whole list -- hosts, user names, key
 * paths and passwords -- is encrypted with the device key and written here, and
 * the NVS copy is erased. The key never leaves NVS, so neither the card alone
 * nor the device alone is enough to read it back. Without the card the app
 * starts with no servers and picks them up again when it is put back. */
#define SRV_SD_DIR  SD_MOUNT "/.tab5"
#define SRV_SD_FILE SRV_SD_DIR "/servers.enc"

static bool save_to_sd(const char *json)
{
    char *blob = secure_encrypt_str(json);
    if (!blob) return false;
    bool ok = fs_mkdir_p(SRV_SD_DIR) && fs_write_all(SRV_SD_FILE, blob, strlen(blob));
    free(blob);
    return ok;
}

static bool save(void)
{
    char *json = server_store_export_json(false);
    if (!json) return false;
    bool ok;
    if (g_settings.creds_on_sd) {
        ok = save_to_sd(json);
        if (ok) nvs_erase_key_ns("srv", "list");   /* leave nothing on the device */
        else ESP_LOGW(TAG, "could not write %s; the list is only in RAM", SRV_SD_FILE);
    } else {
        ok = nvs_set_str_ns("srv", "list", json);
    }
    free(json);
    return ok;
}

void server_store_init(void)
{
    char *json = NULL;
    s_n = 0;
    if (g_settings.creds_on_sd) {
        char *blob = NULL; size_t len = 0;
        if (sd_is_mounted() && fs_read_all(SRV_SD_FILE, &blob, &len, 256 * 1024)) {
            json = secure_decrypt_str(blob);
            free(blob);
            if (!json) ESP_LOGW(TAG, "%s does not decrypt with this device's key", SRV_SD_FILE);
        } else {
            ESP_LOGI(TAG, "credentials live on the TF card and it is not readable; starting with none");
        }
    } else {
        nvs_get_str_alloc("srv", "list", &json);
    }
    if (json) {
        server_store_import_json(json);
        free(json);
        ESP_LOGI(TAG, "%d servers loaded", s_n);
    }
}

void server_store_reload(void) { server_store_init(); }

bool server_store_set_on_sd(bool on)
{
    if ((bool)g_settings.creds_on_sd == on) return true;
    char *json = server_store_export_json(false);
    if (!json) return false;
    bool ok;
    if (on) {
        ok = sd_is_mounted() && save_to_sd(json);
        if (ok) { nvs_erase_key_ns("srv", "list"); g_settings.creds_on_sd = 1; }
    } else {
        ok = nvs_set_str_ns("srv", "list", json);
        if (ok) { unlink(SRV_SD_FILE); g_settings.creds_on_sd = 0; }
    }
    free(json);
    if (ok) settings_save();
    return ok;
}

int server_store_count(void) { return s_n; }
const server_t *server_store_get(int i) { return (i >= 0 && i < s_n) ? &s_list[i] : NULL; }

int server_store_add(const server_t *s)
{
    if (s_n >= SERVER_MAX) return -1;
    int index = s_n++;
    s_list[index] = *s;
    if (!save()) { s_n--; memset(&s_list[index], 0, sizeof(server_t)); return -1; }
    return index;
}
bool server_store_update(int i, const server_t *s)
{
    if (i < 0 || i >= s_n) return false;
    server_t previous = s_list[i];
    s_list[i] = *s;
    if (!save()) { s_list[i] = previous; return false; }
    return true;
}
bool server_store_remove(int i)
{
    if (i < 0 || i >= s_n) return false;
    server_t previous = s_list[i];
    memmove(&s_list[i], &s_list[i + 1], (s_n - i - 1) * sizeof(server_t));
    s_n--;
    if (!save()) {
        memmove(&s_list[i + 1], &s_list[i], (s_n - i) * sizeof(server_t));
        s_list[i] = previous; s_n++; return false;
    }
    return true;
}
void server_store_move(int from, int to)
{
    if (from < 0 || from >= s_n || to < 0 || to >= s_n || from == to) return;
    server_t t = s_list[from];
    if (from < to) memmove(&s_list[from], &s_list[from + 1], (to - from) * sizeof(server_t));
    else memmove(&s_list[to + 1], &s_list[to], (from - to) * sizeof(server_t));
    s_list[to] = t; save();
}

char *server_store_export_json(bool plain)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "servers");
    for (int i = 0; i < s_n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", s_list[i].name);
        cJSON_AddStringToObject(o, "host", s_list[i].host);
        cJSON_AddNumberToObject(o, "port", s_list[i].port);
        cJSON_AddStringToObject(o, "user", s_list[i].user);
        if (plain) cJSON_AddStringToObject(o, "pass", s_list[i].pass);
        else { char *enc = secure_encrypt_str(s_list[i].pass); cJSON_AddStringToObject(o, "pass", enc ? enc : ""); free(enc); }
        cJSON_AddStringToObject(o, "key", s_list[i].keypath);
        cJSON_AddBoolToObject(o, "use_key", s_list[i].use_key);
        cJSON_AddItemToArray(arr, o);
    }
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

bool server_store_import_json(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    cJSON *arr = cJSON_GetObjectItem(root, "servers");
    if (!cJSON_IsArray(arr)) { cJSON_Delete(root); return false; }
    s_n = 0;
    cJSON *o;
    cJSON_ArrayForEach(o, arr) {
        if (s_n >= SERVER_MAX) break;
        server_t *s = &s_list[s_n];
        memset(s, 0, sizeof(*s));
        const char *v;
        if ((v = cJSON_GetStringValue(cJSON_GetObjectItem(o, "name")))) strlcpy(s->name, v, sizeof(s->name));
        if ((v = cJSON_GetStringValue(cJSON_GetObjectItem(o, "host")))) strlcpy(s->host, v, sizeof(s->host));
        cJSON *p = cJSON_GetObjectItem(o, "port"); s->port = cJSON_IsNumber(p) ? p->valueint : 22;
        if ((v = cJSON_GetStringValue(cJSON_GetObjectItem(o, "user")))) strlcpy(s->user, v, sizeof(s->user));
        if ((v = cJSON_GetStringValue(cJSON_GetObjectItem(o, "pass")))) {
            char *dec = secure_decrypt_str(v);
            if (dec) { strlcpy(s->pass, dec, sizeof(s->pass)); free(dec); }
        }
        if ((v = cJSON_GetStringValue(cJSON_GetObjectItem(o, "key")))) strlcpy(s->keypath, v, sizeof(s->keypath));
        s->use_key = cJSON_IsTrue(cJSON_GetObjectItem(o, "use_key"));
        if (!s->host[0]) continue;
        s_n++;
    }
    cJSON_Delete(root);
    save();
    return true;
}
