#include "settings.h"
#include "ui/ui_theme.h"
#include "input/keyboard.h"
#include <string.h>
#include <stdlib.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "settings";
#define NS "app"
#define KEY "cfg"
#define CFG_VERSION 16

app_settings_t g_settings;

typedef struct {
    uint32_t magic;
    uint32_t version;
    app_settings_t s;
} cfg_blob_t;

void settings_reset_defaults(void)
{
    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.lang = LANG_UNSET;
    g_settings.ime_src = IME_SRC_EN;
    g_settings.ime_ja_mode = IME_JA_HIRA;
    g_settings.rotation = 1;            /* landscape, USB-C on the left */
    g_settings.brightness = 80;
    g_settings.ss_enable = 1;
    g_settings.ss_idle_min = 5;
    g_settings.ss_switch_min = 1;
    g_settings.ss_disable_in_ssh = 1;
    strcpy(g_settings.ss_wallpaper_dir, "");
    g_settings.ss_elem_x[0] = 40;  g_settings.ss_elem_y[0] = 40;   /* clock */
    g_settings.ss_elem_x[1] = 40;  g_settings.ss_elem_y[1] = 160;  /* date */
    g_settings.ss_elem_x[2] = 40;  g_settings.ss_elem_y[2] = 220;  /* battery */
    g_settings.ss_elem_x[3] = 40;  g_settings.ss_elem_y[3] = 270;  /* network */
    for (int i = 0; i < 4; i++) g_settings.ss_elem_color[i] = 0xFFFFFF;
    g_settings.utc_offset_min = 9 * 60;
    g_settings.kbd_led_mode = 0;
    g_settings.kbd_led_bright = 20;
    g_settings.kbd_led_rgb[0] = 0; g_settings.kbd_led_rgb[1] = 128; g_settings.kbd_led_rgb[2] = 255;
    g_settings.status_items = STATUS_DEFAULT;
    g_settings.creds_on_sd = 0;
    g_settings.term_cursor_style = 0;
    g_settings.term_cursor_blink = 1;
    g_settings.term_line_height = 32;   /* 24 px glyphs with readable row spacing */
    /* The look, including the terminal palette, comes from preset 0. */
    ui_theme_set_preset(0);
    g_settings.term_font = 0;
    g_settings.volume = 60;
    g_settings.webfm_autostart = 0;
    g_settings.ime_skk_sd = 1;
    strcpy(g_settings.ota_url, "https://raw.githubusercontent.com/Mozgi512/Clientre5/main/update.json");
    g_settings.ssh_bell = 1;
    g_settings.ts_enable = 0;
    g_settings.ts_direct = 0;
    strcpy(g_settings.ts_device_name, "clientre5");
}

void settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS erase + reinit");
        nvs_flash_erase();
        nvs_flash_init();
    }
    settings_reset_defaults();
    void *blob = NULL; size_t len = 0;
    if (nvs_get_blob_alloc(NS, KEY, &blob, &len)) {
        cfg_blob_t *b = blob;
        if (len >= sizeof(cfg_blob_t) && b->magic == 0x54414235 && b->version == CFG_VERSION) {
            g_settings = b->s;
        } else if (len >= sizeof(uint32_t) * 2 && b->magic == 0x54414235) {
            /* older layout: copy what fits, keep defaults for the rest */
            uint32_t old_version = b->version;
            size_t n = len - 2 * sizeof(uint32_t);
            if (n > sizeof(app_settings_t)) n = sizeof(app_settings_t);
            memcpy(&g_settings, &b->s, n);
            /* Previous releases left existing compact values unchanged unless
             * they were exactly 26. Give all old compact settings breathing room;
             * preserve larger spacing and allow subsequent manual adjustments. */
            if (old_version <= 11 && g_settings.term_line_height <= 30)
                g_settings.term_line_height = 32;
            if (old_version <= 12) ui_theme_set_terminal_amber();
            if (old_version <= 13 && (g_settings.term_line_height == 34 || g_settings.term_line_height < 28))
                g_settings.term_line_height = 32;
            if (old_version <= 14) g_settings.ss_classic = 0;
            if (old_version <= 15) g_settings.kbd_layout = KBD_LAYOUT_STOCK;
            settings_save();
        }
        free(blob);
    }
    if (g_settings.brightness < 5) g_settings.brightness = 5;
    if (g_settings.ss_idle_min == 0) g_settings.ss_idle_min = 5;
    if (g_settings.lang != LANG_UNSET && g_settings.lang >= LANG_COUNT) g_settings.lang = LANG_EN;
    i18n_set_lang(g_settings.lang == LANG_UNSET ? LANG_EN : g_settings.lang);
}

void settings_save(void)
{
    cfg_blob_t b = { .magic = 0x54414235, .version = CFG_VERSION, .s = g_settings };
    nvs_set_blob_ns(NS, KEY, &b, sizeof(b));
}

bool nvs_get_blob_alloc(const char *ns, const char *key, void **out, size_t *len)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
    size_t n = 0;
    bool ok = false;
    if (nvs_get_blob(h, key, NULL, &n) == ESP_OK && n > 0) {
        void *p = malloc(n);
        if (p && nvs_get_blob(h, key, p, &n) == ESP_OK) { *out = p; *len = n; ok = true; }
        else free(p);
    }
    nvs_close(h);
    return ok;
}

bool nvs_set_blob_ns(const char *ns, const char *key, const void *data, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_blob(h, key, data, len);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGE(TAG, "nvs_set_blob %s/%s: %s", ns, key, esp_err_to_name(e));
    return e == ESP_OK;
}

bool nvs_erase_key_ns(const char *ns, const char *key)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_erase_key(h, key);
    nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}

bool nvs_get_str_alloc(const char *ns, const char *key, char **out)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
    size_t n = 0; bool ok = false;
    if (nvs_get_str(h, key, NULL, &n) == ESP_OK && n > 0) {
        char *p = malloc(n);
        if (p && nvs_get_str(h, key, p, &n) == ESP_OK) { *out = p; ok = true; }
        else free(p);
    }
    nvs_close(h);
    return ok;
}

bool nvs_set_str_ns(const char *ns, const char *key, const char *val)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_str(h, key, val);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}
