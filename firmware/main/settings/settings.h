#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "i18n/i18n.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { IME_SRC_EN = 0, IME_SRC_JA = 1, IME_SRC_ZH = 2, IME_SRC_COUNT } ime_src_t;
typedef enum { IME_JA_HIRA = 0, IME_JA_KATA = 1 } ime_ja_mode_t;

/* Status bar elements, as a bitmask in app_settings_t.status_items. */
#define STATUS_WIFI      (1u << 0)
#define STATUS_TAILSCALE (1u << 1)
#define STATUS_IP        (1u << 2)
#define STATUS_BATTERY   (1u << 3)
#define STATUS_KEYBOARD  (1u << 4)
#define STATUS_CLOCK     (1u << 5)
#define STATUS_DATE      (1u << 6)
#define STATUS_YEAR      (1u << 7)
#define STATUS_DEFAULT   (STATUS_WIFI | STATUS_TAILSCALE | STATUS_IP | STATUS_BATTERY | \
                          STATUS_KEYBOARD | STATUS_CLOCK | STATUS_DATE)

typedef struct {
    uint8_t  lang;              /* lang_t, LANG_UNSET on first boot */
    uint8_t  ime_src;           /* ime_src_t */
    uint8_t  ime_ja_mode;       /* ime_ja_mode_t */
    uint8_t  rotation;          /* 0..3 -> lv_display_rotation_t */
    uint8_t  brightness;        /* 5..100 */
    uint8_t  ss_enable;
    uint16_t ss_idle_min;
    uint16_t ss_switch_min;
    uint8_t  ss_disable_in_ssh;
    char     ss_wallpaper_dir[96];
    int16_t  ss_elem_x[4];
    int16_t  ss_elem_y[4];
    uint32_t ss_elem_color[4];
    int16_t  utc_offset_min;
    uint8_t  kbd_led_mode;      /* 0=bind, 1=user */
    uint8_t  kbd_led_bright;    /* 0..100 */
    uint8_t  kbd_led_rgb[3];
    uint8_t  term_font;         /* 0=22px (only size shipped) */
    uint8_t  volume;            /* 0..100 */
    uint8_t  webfm_autostart;
    uint8_t  ime_skk_sd;        /* prefer SD dictionary when present */
    char     ota_url[160];
    uint8_t  ssh_bell;
    uint8_t  ts_enable;
    char     ts_auth_key[160];
    char     ts_device_name[40];
    char     ts_ctrl_host[96];
    uint8_t  ts_direct;          /* try direct UDP paths (experimental); 0 = DERP relay only */
    uint8_t  status_items;       /* STATUS_* bitmask */
    uint8_t  creds_on_sd;        /* keep the SSH server list on the TF card, not in NVS */
    uint8_t  theme_preset;       /* index into the preset table, UI_THEME_CUSTOM for custom */
    uint32_t theme_colors[8];    /* used when the preset is custom */
    uint8_t  theme_radius;       /* corner rounding, 0..24 */
    uint8_t  theme_density;      /* ui_density_t: padding scale */
    uint8_t  theme_tile_accent;  /* tint the home tiles with the accent colour */
    uint8_t  theme_glow;         /* coloured halo behind cards and buttons */
    uint8_t  theme_scanlines;    /* CRT/EL scan lines drawn over every screen */
    uint32_t term_fg, term_bg, term_cursor;
    uint32_t term_ansi[16];
    uint8_t  term_cursor_style;  /* 0 block, 1 underline, 2 bar */
    uint8_t  term_cursor_blink;
    uint8_t  term_line_height;   /* 28..40 px, MaruMinya 24px glyphs + leading */
    uint8_t ss_classic;          /* 0: standby cards, 1: movable legacy widgets */
    uint8_t  kbd_layout;         /* kbd_layout_t: STOCK follows the keycaps */
} app_settings_t;

extern app_settings_t g_settings;

void settings_init(void);         /* load from NVS or defaults */
void settings_save(void);
void settings_reset_defaults(void);

/* Generic small NVS helpers used by other modules. */
bool nvs_get_blob_alloc(const char *ns, const char *key, void **out, size_t *len);
bool nvs_set_blob_ns(const char *ns, const char *key, const void *data, size_t len);
bool nvs_erase_key_ns(const char *ns, const char *key);
bool nvs_get_str_alloc(const char *ns, const char *key, char **out);
bool nvs_set_str_ns(const char *ns, const char *key, const char *val);

#ifdef __cplusplus
}
#endif
