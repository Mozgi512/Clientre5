#include "wifi_mgr.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "cJSON.h"
#include "settings/settings.h"
#include "bsp/m5stack_tab5.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi";
#define NS "wifi"

static wifi_saved_t s_saved[WIFI_MAX_SAVED];
static int s_saved_n = 0;
static char s_last_ssid[33];
static bool s_started = false, s_connected = false, s_connecting = false, s_scanning = false;
static char s_cur_ssid[33];
static esp_netif_t *s_netif = NULL;
static wifi_ev_cb_t s_cb = NULL; static void *s_cb_user = NULL;
static wifi_ev_cb_t s_gcb = NULL; static void *s_gcb_user = NULL;
static int s_retry = 0;
static char s_err[64];
static wifi_scan_item_t s_scan[24]; static int s_scan_n = 0;
static bool s_user_disconnect = false;

static void emit(wifi_ev_t ev) { if (s_gcb) s_gcb(ev, s_gcb_user); if (s_cb) s_cb(ev, s_cb_user); }

static void saved_load(void)
{
    void *blob = NULL; size_t len = 0;
    s_saved_n = 0;
    if (nvs_get_blob_alloc(NS, "list", &blob, &len)) {
        int n = len / sizeof(wifi_saved_t);
        if (n > WIFI_MAX_SAVED) n = WIFI_MAX_SAVED;
        memcpy(s_saved, blob, n * sizeof(wifi_saved_t));
        s_saved_n = n;
        free(blob);
    }
    char *last = NULL;
    if (nvs_get_str_alloc(NS, "last", &last)) { strlcpy(s_last_ssid, last, sizeof(s_last_ssid)); free(last); }
}
static void saved_store(void)
{
    nvs_set_blob_ns(NS, "list", s_saved, s_saved_n * sizeof(wifi_saved_t));
    nvs_set_str_ns(NS, "last", s_last_ssid);
}
static int saved_find(const char *ssid)
{
    for (int i = 0; i < s_saved_n; i++) if (!strcmp(s_saved[i].ssid, ssid)) return i;
    return -1;
}
static void saved_put(const char *ssid, const char *pass)
{
    int i = saved_find(ssid);
    if (i < 0) {
        if (s_saved_n >= WIFI_MAX_SAVED) { memmove(&s_saved[0], &s_saved[1], (WIFI_MAX_SAVED - 1) * sizeof(wifi_saved_t)); s_saved_n = WIFI_MAX_SAVED - 1; }
        i = s_saved_n++;
    }
    strlcpy(s_saved[i].ssid, ssid, sizeof(s_saved[i].ssid));
    strlcpy(s_saved[i].pass, pass ? pass : "", sizeof(s_saved[i].pass));
    strlcpy(s_last_ssid, ssid, sizeof(s_last_ssid));
    saved_store();
}

static void do_connect(const char *ssid, const char *pass)
{
    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, pass ? pass : "", sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = (pass && *pass) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    strlcpy(s_cur_ssid, ssid, sizeof(s_cur_ssid));
    s_connecting = true; s_user_disconnect = false; s_retry = 0;
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_err_t e = esp_wifi_connect();
    if (e != ESP_OK) ESP_LOGW(TAG, "connect: %s", esp_err_to_name(e));
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            s_started = true;
            if (s_last_ssid[0]) { int i = saved_find(s_last_ssid); if (i >= 0) do_connect(s_saved[i].ssid, s_saved[i].pass); }
            break;
        case WIFI_EVENT_STA_CONNECTED:
            esp_netif_create_ip6_linklocal(s_netif);
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *d = data;
            bool was = s_connected;
            s_connected = false;
            if (s_user_disconnect) { s_connecting = false; emit(WIFI_EV_DISCONNECTED); break; }
            if (s_connecting && s_retry < 3) { s_retry++; esp_wifi_connect(); break; }
            if (s_connecting) {
                s_connecting = false;
                snprintf(s_err, sizeof(s_err), "reason %d", d ? d->reason : -1);
                emit(WIFI_EV_CONNECT_FAILED);
            } else if (was) {
                emit(WIFI_EV_DISCONNECTED);
                /* background reconnect */
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_wifi_connect();
            }
            break;
        }
        case WIFI_EVENT_SCAN_DONE: {
            uint16_t n = sizeof(s_scan) / sizeof(s_scan[0]);
            wifi_ap_record_t *recs = calloc(n, sizeof(wifi_ap_record_t));
            s_scan_n = 0;
            if (recs && esp_wifi_scan_get_ap_records(&n, recs) == ESP_OK) {
                for (int i = 0; i < n; i++) {
                    if (!recs[i].ssid[0]) continue;
                    bool dup = false;
                    for (int j = 0; j < s_scan_n; j++) if (!strcmp(s_scan[j].ssid, (char *)recs[i].ssid)) { dup = true; break; }
                    if (dup) continue;
                    strlcpy(s_scan[s_scan_n].ssid, (char *)recs[i].ssid, 33);
                    s_scan[s_scan_n].rssi = recs[i].rssi;
                    s_scan[s_scan_n].auth = recs[i].authmode;
                    s_scan[s_scan_n].channel = recs[i].primary;
                    s_scan_n++;
                }
            }
            free(recs);
            esp_wifi_clear_ap_list();
            s_scanning = false;
            emit(WIFI_EV_SCAN_DONE);
            break;
        }
        default: break;
        }
    } else if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) {
            s_connected = true; s_connecting = false; s_retry = 0;
            emit(WIFI_EV_CONNECTED);
        } else if (id == IP_EVENT_GOT_IP6) {
            emit(WIFI_EV_GOT_IP6);
        }
    }
}

void wifi_mgr_init(void)
{
    saved_load();
    esp_netif_init();
    esp_event_loop_create_default();
    s_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(s_netif, "tab5");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t e = esp_wifi_init(&cfg);
    if (e != ESP_OK) { ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(e)); return; }
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, on_wifi, NULL);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    e = esp_wifi_start();
    if (e != ESP_OK) ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(e));
}

bool wifi_mgr_is_connected(void) { return s_connected; }
bool wifi_mgr_is_connecting(void) { return s_connecting; }
bool wifi_mgr_is_started(void) { return s_started; }
const char *wifi_mgr_ssid(void) { return s_connected ? s_cur_ssid : ""; }

void wifi_mgr_get_ip4(char *buf, size_t len)
{
    buf[0] = 0;
    if (!s_connected || !s_netif) return;
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(s_netif, &ip) == ESP_OK) snprintf(buf, len, IPSTR, IP2STR(&ip.ip));
}

void wifi_mgr_get_ip6(char *buf, size_t len)
{
    buf[0] = 0;
    if (!s_connected || !s_netif) return;
    esp_ip6_addr_t addrs[5];
    int n = esp_netif_get_all_ip6(s_netif, addrs);
    /* prefer global */
    for (int pass = 0; pass < 2 && !buf[0]; pass++) {
        for (int i = 0; i < n; i++) {
            esp_ip6_addr_type_t t = esp_netif_ip6_get_addr_type(&addrs[i]);
            bool global = (t == ESP_IP6_ADDR_IS_GLOBAL || t == ESP_IP6_ADDR_IS_UNIQUE_LOCAL);
            if ((pass == 0 && global) || pass == 1) { snprintf(buf, len, IPV6STR, IPV62STR(addrs[i])); break; }
        }
    }
}

int wifi_mgr_rssi(void)
{
    wifi_ap_record_t ap;
    if (s_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return ap.rssi;
    return 0;
}
int wifi_mgr_channel(void)
{
    wifi_ap_record_t ap;
    if (s_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return ap.primary;
    return 0;
}

void wifi_mgr_connect(const char *ssid, const char *pass, bool save)
{
    if (!s_started) return;
    if (save) saved_put(ssid, pass);
    if (s_connected || s_connecting) { s_user_disconnect = true; esp_wifi_disconnect(); vTaskDelay(pdMS_TO_TICKS(200)); }
    do_connect(ssid, pass);
}

void wifi_mgr_disconnect(void)
{
    s_user_disconnect = true;
    esp_wifi_disconnect();
}

bool wifi_mgr_scan_start(void)
{
    if (!s_started || s_scanning) return false;
    wifi_scan_config_t sc = { .show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE };
    sc.scan_time.active.min = 60; sc.scan_time.active.max = 150;
    esp_err_t e = esp_wifi_scan_start(&sc, false);
    s_scanning = (e == ESP_OK);
    if (e != ESP_OK) ESP_LOGW(TAG, "scan: %s", esp_err_to_name(e));
    return s_scanning;
}

int wifi_mgr_scan_results(wifi_scan_item_t *out, int max)
{
    int n = s_scan_n < max ? s_scan_n : max;
    memcpy(out, s_scan, n * sizeof(*out));
    return n;
}

int wifi_mgr_saved_count(void) { return s_saved_n; }
const wifi_saved_t *wifi_mgr_saved(int i) { return (i >= 0 && i < s_saved_n) ? &s_saved[i] : NULL; }
void wifi_mgr_forget(int i)
{
    if (i < 0 || i >= s_saved_n) return;
    if (!strcmp(s_saved[i].ssid, s_last_ssid)) s_last_ssid[0] = 0;
    memmove(&s_saved[i], &s_saved[i + 1], (s_saved_n - i - 1) * sizeof(wifi_saved_t));
    s_saved_n--;
    saved_store();
}
void wifi_mgr_set_cb(wifi_ev_cb_t cb, void *user) { s_cb = cb; s_cb_user = user; }
void wifi_mgr_set_global_cb(wifi_ev_cb_t cb, void *user) { s_gcb = cb; s_gcb_user = user; }
const char *wifi_mgr_last_error(void) { return s_err; }

char *wifi_mgr_export_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "networks");
    for (int i = 0; i < s_saved_n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", s_saved[i].ssid);
        cJSON_AddStringToObject(o, "pass", s_saved[i].pass);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddStringToObject(root, "last", s_last_ssid);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

bool wifi_mgr_import_json(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    cJSON *arr = cJSON_GetObjectItem(root, "networks");
    if (cJSON_IsArray(arr)) {
        s_saved_n = 0;
        cJSON *o;
        cJSON_ArrayForEach(o, arr) {
            const char *ssid = cJSON_GetStringValue(cJSON_GetObjectItem(o, "ssid"));
            const char *pass = cJSON_GetStringValue(cJSON_GetObjectItem(o, "pass"));
            if (ssid && s_saved_n < WIFI_MAX_SAVED) {
                strlcpy(s_saved[s_saved_n].ssid, ssid, 33);
                strlcpy(s_saved[s_saved_n].pass, pass ? pass : "", 65);
                s_saved_n++;
            }
        }
    }
    const char *last = cJSON_GetStringValue(cJSON_GetObjectItem(root, "last"));
    if (last) strlcpy(s_last_ssid, last, sizeof(s_last_ssid));
    saved_store();
    cJSON_Delete(root);
    return true;
}
