/* Web file manager: HTTP server exposing the TF card / USB drive. */
#include "webfm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_flash.h"
#include "cJSON.h"
#include "storage/sd.h"
#include "sys/sysinfo.h"
#include "sys/usb_mgr.h"
#include "net/wifi_mgr.h"
#include "input/keyboard.h"
#include "settings/settings.h"

static const char *TAG = "webfm";
extern const uint8_t webfm_html_start[] asm("_binary_webfm_html_start");
extern const uint8_t webfm_html_end[]   asm("_binary_webfm_html_end");
static httpd_handle_t s_srv = NULL;
static const char *ui_basename_web(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }

static const char *root_dir(void) { return usb_mgr_msc_mounted() && !sd_is_mounted() ? USB_MOUNT : SD_MOUNT; }

static bool q_param(httpd_req_t *r, const char *key, char *out, size_t cap)
{
    size_t ql = httpd_req_get_url_query_len(r) + 1;
    if (ql <= 1 || ql > 2048) { out[0] = 0; return false; }
    char *q = malloc(ql);
    bool ok = false;
    if (httpd_req_get_url_query_str(r, q, ql) == ESP_OK) {
        char *enc = malloc(cap);
        if (httpd_query_key_value(q, key, enc, cap) == ESP_OK) {
            /* URL decode */
            size_t o = 0;
            for (size_t i = 0; enc[i] && o + 1 < cap; i++) {
                if (enc[i] == '%' && enc[i + 1] && enc[i + 2]) { char h[3] = { enc[i + 1], enc[i + 2], 0 }; out[o++] = (char)strtol(h, NULL, 16); i += 2; }
                else if (enc[i] == '+') out[o++] = ' ';
                else out[o++] = enc[i];
            }
            out[o] = 0; ok = true;
        } else out[0] = 0;
        free(enc);
    } else out[0] = 0;
    free(q);
    return ok;
}

/* Build an absolute, sanitized path under the root. */
static bool safe_path(const char *rel, char *out, size_t cap)
{
    if (strstr(rel, "..")) return false;
    while (*rel == '/') rel++;
    if (*rel) snprintf(out, cap, "%s/%s", root_dir(), rel); else snprintf(out, cap, "%s", root_dir());
    size_t l = strlen(out);
    while (l > 1 && out[l - 1] == '/') out[--l] = 0;
    return true;
}

static esp_err_t send_err(httpd_req_t *r, const char *code, const char *msg)
{
    httpd_resp_set_status(r, code);
    httpd_resp_set_type(r, "text/plain; charset=utf-8");
    return httpd_resp_send(r, msg, HTTPD_RESP_USE_STRLEN);
}
static esp_err_t send_ok(httpd_req_t *r) { httpd_resp_set_type(r, "text/plain"); return httpd_resp_send(r, "ok", 2); }

static esp_err_t h_root(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_send(r, (const char *)webfm_html_start, webfm_html_end - webfm_html_start - 1);
}

static esp_err_t h_list(httpd_req_t *r)
{
    char rel[512], path[600];
    q_param(r, "dir", rel, sizeof(rel));
    if (!safe_path(rel, path, sizeof(path))) return send_err(r, "400 Bad Request", "Bad Path");
    DIR *d = opendir(path);
    if (!d) return send_err(r, "404 Not Found", "Cannot read folder");
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "items");
    struct dirent *de; char full[900];
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", de->d_name);
        bool isdir = de->d_type == DT_DIR;
        if (!isdir) { snprintf(full, sizeof(full), "%s/%s", path, de->d_name); struct stat st; if (stat(full, &st) == 0) { isdir = S_ISDIR(st.st_mode); cJSON_AddNumberToObject(o, "size", (double)st.st_size); } }
        cJSON_AddBoolToObject(o, "dir", isdir);
        cJSON_AddItemToArray(arr, o);
    }
    closedir(d);
    uint64_t tot, fr;
    if (sd_space(root_dir(), &tot, &fr)) { cJSON_AddNumberToObject(root, "total", (double)tot); cJSON_AddNumberToObject(root, "free", (double)fr); }
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(r, "application/json");
    esp_err_t e = httpd_resp_send(r, s, HTTPD_RESP_USE_STRLEN);
    free(s);
    return e;
}

static const char *mime_for(const char *name)
{
    const char *x = fs_ext(name);
    if (!strcmp(x, "jpg") || !strcmp(x, "jpeg")) return "image/jpeg";
    if (!strcmp(x, "png")) return "image/png";
    if (!strcmp(x, "gif")) return "image/gif";
    if (!strcmp(x, "bmp")) return "image/bmp";
    if (!strcmp(x, "txt") || !strcmp(x, "log") || !strcmp(x, "md")) return "text/plain; charset=utf-8";
    if (!strcmp(x, "html")) return "text/html; charset=utf-8";
    if (!strcmp(x, "json")) return "application/json";
    if (!strcmp(x, "mp3")) return "audio/mpeg";
    if (!strcmp(x, "flac")) return "audio/flac";
    if (!strcmp(x, "wav")) return "audio/wav";
    return "application/octet-stream";
}

static esp_err_t send_file(httpd_req_t *r, bool attachment)
{
    char rel[512], path[600];
    q_param(r, "path", rel, sizeof(rel));
    if (!safe_path(rel, path, sizeof(path))) return send_err(r, "400 Bad Request", "Bad Path");
    FILE *f = fopen(path, "rb");
    if (!f) return send_err(r, "404 Not Found", "Cannot read file.");
    httpd_resp_set_type(r, mime_for(path));
    if (attachment) {
        char hdr[400]; snprintf(hdr, sizeof(hdr), "attachment; filename*=UTF-8''%s", ui_basename_web(path));
        httpd_resp_set_hdr(r, "Content-Disposition", hdr);
    }
    size_t bs = 32 * 1024; char *buf = malloc(bs); size_t n;
    esp_err_t e = ESP_OK;
    while ((n = fread(buf, 1, bs, f)) > 0) { if ((e = httpd_resp_send_chunk(r, buf, n)) != ESP_OK) break; }
    free(buf); fclose(f);
    if (e == ESP_OK) httpd_resp_send_chunk(r, NULL, 0);
    return e;
}
static esp_err_t h_download(httpd_req_t *r) { return send_file(r, true); }
static esp_err_t h_image(httpd_req_t *r) { return send_file(r, false); }
static esp_err_t h_read(httpd_req_t *r)
{
    char rel[512], path[600];
    q_param(r, "path", rel, sizeof(rel));
    if (!safe_path(rel, path, sizeof(path))) return send_err(r, "400 Bad Request", "Bad Path");
    struct stat st; if (stat(path, &st) != 0) return send_err(r, "404 Not Found", "Cannot read file.");
    if (st.st_size > 512 * 1024) return send_err(r, "413 Payload Too Large", "File too large to edit");
    return send_file(r, false);
}

static esp_err_t h_save(httpd_req_t *r)
{
    char rel[512], path[600];
    q_param(r, "path", rel, sizeof(rel));
    if (!safe_path(rel, path, sizeof(path))) return send_err(r, "400 Bad Request", "Bad Path");
    FILE *f = fopen(path, "wb");
    if (!f) return send_err(r, "500 Internal Server Error", "Cannot write file");
    char *buf = malloc(8192); int remaining = r->content_len;
    while (remaining > 0) {
        int n = httpd_req_recv(r, buf, remaining > 8192 ? 8192 : remaining);
        if (n <= 0) { free(buf); fclose(f); return send_err(r, "500 Internal Server Error", "Transfer failed"); }
        fwrite(buf, 1, n, f); remaining -= n;
    }
    free(buf); fclose(f);
    return send_ok(r);
}

static esp_err_t h_upload(httpd_req_t *r)
{
    char rel[512], name[256], off_s[24], tot_s[24], path[900];
    q_param(r, "dir", rel, sizeof(rel)); q_param(r, "name", name, sizeof(name));
    q_param(r, "offset", off_s, sizeof(off_s)); q_param(r, "total", tot_s, sizeof(tot_s));
    if (!name[0] || strchr(name, '/') || strstr(name, "..")) return send_err(r, "400 Bad Request", "Bad filename.");
    if (!safe_path(rel, path, sizeof(path))) return send_err(r, "400 Bad Request", "Bad Path");
    strlcat(path, "/", sizeof(path)); strlcat(path, name, sizeof(path));
    long off = atol(off_s);
    FILE *f = fopen(path, off == 0 ? "wb" : "r+b");
    if (!f && off > 0) f = fopen(path, "wb");
    if (!f) return send_err(r, "500 Internal Server Error", "Cannot write file");
    if (off > 0) fseek(f, off, SEEK_SET);
    int remaining = r->content_len;
    char *buf = malloc(16384);
    while (remaining > 0) {
        int n = httpd_req_recv(r, buf, remaining > 16384 ? 16384 : remaining);
        if (n <= 0) { if (n == HTTPD_SOCK_ERR_TIMEOUT) continue; free(buf); fclose(f); return send_err(r, "500 Internal Server Error", "Transfer failed"); }
        if (fwrite(buf, 1, n, f) != (size_t)n) { free(buf); fclose(f); return send_err(r, "500 Internal Server Error", "Write failed"); }
        remaining -= n;
    }
    free(buf); fclose(f);
    return send_ok(r);
}

static esp_err_t h_mkdir(httpd_req_t *r)
{
    char rel[512], name[256], path[900];
    q_param(r, "dir", rel, sizeof(rel)); q_param(r, "name", name, sizeof(name));
    if (!name[0] || strchr(name, '/') || !safe_path(rel, path, sizeof(path))) return send_err(r, "400 Bad Request", "Bad Path");
    strlcat(path, "/", sizeof(path)); strlcat(path, name, sizeof(path));
    if (mkdir(path, 0777) != 0) return send_err(r, "500 Internal Server Error", strerror(errno));
    return send_ok(r);
}
static esp_err_t h_newfile(httpd_req_t *r)
{
    char rel[512], name[256], path[900];
    q_param(r, "dir", rel, sizeof(rel)); q_param(r, "name", name, sizeof(name));
    if (!name[0] || strchr(name, '/') || !safe_path(rel, path, sizeof(path))) return send_err(r, "400 Bad Request", "Bad Path");
    strlcat(path, "/", sizeof(path)); strlcat(path, name, sizeof(path));
    if (fs_exists(path)) return send_err(r, "409 Conflict", "Target exists");
    FILE *f = fopen(path, "wb"); if (!f) return send_err(r, "500 Internal Server Error", strerror(errno)); fclose(f);
    return send_ok(r);
}
static esp_err_t h_delete(httpd_req_t *r)
{
    char rel[512], path[600];
    q_param(r, "path", rel, sizeof(rel));
    if (!rel[0] || !safe_path(rel, path, sizeof(path))) return send_err(r, "400 Bad Request", "Bad Path");
    int rc = fs_remove_recursive(path);
    if (rc) return send_err(r, "500 Internal Server Error", strerror(-rc));
    return send_ok(r);
}
static esp_err_t h_rename(httpd_req_t *r)
{
    char rel[512], name[256], path[600], dst[900];
    q_param(r, "path", rel, sizeof(rel)); q_param(r, "name", name, sizeof(name));
    if (!rel[0] || !name[0] || strchr(name, '/') || !safe_path(rel, path, sizeof(path))) return send_err(r, "400 Bad Request", "Bad Path");
    strlcpy(dst, path, sizeof(dst)); char *sl = strrchr(dst, '/'); if (sl) *(sl + 1) = 0; strlcat(dst, name, sizeof(dst));
    if (fs_exists(dst)) return send_err(r, "409 Conflict", "Target exists");
    if (rename(path, dst) != 0) return send_err(r, "500 Internal Server Error", strerror(errno));
    return send_ok(r);
}
static esp_err_t h_move(httpd_req_t *r)
{
    char rel[512], drel[512], path[600], dst[900];
    q_param(r, "path", rel, sizeof(rel)); q_param(r, "dir", drel, sizeof(drel));
    if (!rel[0] || !safe_path(rel, path, sizeof(path)) || !safe_path(drel, dst, sizeof(dst))) return send_err(r, "400 Bad Request", "Bad Path");
    if (!fs_is_dir(dst)) return send_err(r, "404 Not Found", "Target folder missing");
    strlcat(dst, "/", sizeof(dst)); strlcat(dst, ui_basename_web(path), sizeof(dst));
    if (!strcmp(path, dst)) return send_ok(r);
    if (!strncmp(dst, path, strlen(path)) && dst[strlen(path)] == '/') return send_err(r, "400 Bad Request", "Cannot move into itself");
    if (fs_exists(dst)) return send_err(r, "409 Conflict", "Target exists");
    if (rename(path, dst) != 0) return send_err(r, "500 Internal Server Error", strerror(errno));
    return send_ok(r);
}

static esp_err_t h_sysinfo(httpd_req_t *r)
{
    cJSON *o = cJSON_CreateObject();
    char b[96];
    esp_chip_info_t ci; esp_chip_info(&ci);
    snprintf(b, sizeof(b), "ESP32-P4 rev %d.%d, %d cores", ci.revision / 100, ci.revision % 100, ci.cores);
    cJSON_AddStringToObject(o, "Chip", b);
    cJSON_AddStringToObject(o, "IDF", esp_get_idf_version());
    cJSON_AddStringToObject(o, "App", sysinfo_app_version());
    uint32_t fsz = 0; esp_flash_get_size(NULL, &fsz); snprintf(b, sizeof(b), "%lu MB", (unsigned long)(fsz >> 20)); cJSON_AddStringToObject(o, "Flash", b);
    uint32_t up = sysinfo_uptime_s(); snprintf(b, sizeof(b), "%lu:%02lu:%02lu", (unsigned long)up / 3600, (unsigned long)(up / 60) % 60, (unsigned long)up % 60); cJSON_AddStringToObject(o, "Uptime", b);
    size_t df, dt, mf, mt, pf, pt; sysinfo_mem(&df, &dt, &mf, &mt, &pf, &pt);
    snprintf(b, sizeof(b), "%u / %u KB", (unsigned)(df >> 10), (unsigned)(dt >> 10)); cJSON_AddStringToObject(o, "DRAM free/total", b);
    snprintf(b, sizeof(b), "%u / %u KB", (unsigned)(mf >> 10), (unsigned)(mt >> 10)); cJSON_AddStringToObject(o, "DMA free/total", b);
    snprintf(b, sizeof(b), "%u / %u KB", (unsigned)(pf >> 10), (unsigned)(pt >> 10)); cJSON_AddStringToObject(o, "PSRAM free/total", b);
    char ip4[48], ip6[64]; wifi_mgr_get_ip4(ip4, sizeof(ip4)); wifi_mgr_get_ip6(ip6, sizeof(ip6));
    snprintf(b, sizeof(b), "%s  %s  %d dBm ch%d", wifi_mgr_ssid(), ip4, wifi_mgr_rssi(), wifi_mgr_channel()); cJSON_AddStringToObject(o, "WiFi", b);
    cJSON_AddStringToObject(o, "IPv6", ip6[0] ? ip6 : "-");
    sd_card_info(b, sizeof(b)); cJSON_AddStringToObject(o, "TF card", b);
    power_info_t p = sysinfo_power();
    if (p.valid) { snprintf(b, sizeof(b), "%.2fV %.2fA %.1fW (%d%%) %s", p.bus_v, p.current_a, p.power_w, p.percent, sysinfo_power_src_text()); cJSON_AddStringToObject(o, "Power", b); }
    cJSON_AddStringToObject(o, "Keyboard", keyboard_present() ? "Tab5 keyboard" : (keyboard_usb_present() ? "USB HID" : "none"));
    char *s = cJSON_PrintUnformatted(o); cJSON_Delete(o);
    httpd_resp_set_type(r, "application/json");
    esp_err_t e = httpd_resp_send(r, s, HTTPD_RESP_USE_STRLEN);
    free(s); return e;
}

void webfm_init(void)
{
    if (g_settings.webfm_autostart) { /* started once WiFi connects (from UI/webfm screen) */ }
}

bool webfm_start(void)
{
    if (s_srv) return true;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80; cfg.max_uri_handlers = 20; cfg.stack_size = 12288; cfg.recv_wait_timeout = 15; cfg.send_wait_timeout = 15;
    cfg.lru_purge_enable = true; cfg.max_open_sockets = 6;
    if (httpd_start(&s_srv, &cfg) != ESP_OK) { ESP_LOGE(TAG, "httpd_start failed"); s_srv = NULL; return false; }
    const httpd_uri_t uris[] = {
        { "/", HTTP_GET, h_root, NULL }, { "/webfm", HTTP_GET, h_root, NULL },
        { "/webfm/list", HTTP_GET, h_list, NULL }, { "/webfm/download", HTTP_GET, h_download, NULL },
        { "/webfm/image", HTTP_GET, h_image, NULL }, { "/webfm/read", HTTP_GET, h_read, NULL },
        { "/webfm/save", HTTP_POST, h_save, NULL }, { "/webfm/upload", HTTP_POST, h_upload, NULL },
        { "/webfm/mkdir", HTTP_GET, h_mkdir, NULL }, { "/webfm/newfile", HTTP_GET, h_newfile, NULL },
        { "/webfm/delete", HTTP_GET, h_delete, NULL }, { "/webfm/rename", HTTP_GET, h_rename, NULL },
        { "/webfm/move", HTTP_GET, h_move, NULL }, { "/webfm/sysinfo", HTTP_GET, h_sysinfo, NULL },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) httpd_register_uri_handler(s_srv, &uris[i]);
    ESP_LOGI(TAG, "web file manager started");
    return true;
}

void webfm_stop(void)
{
    if (!s_srv) return;
    httpd_stop(s_srv); s_srv = NULL;
}
bool webfm_running(void) { return s_srv != NULL; }
