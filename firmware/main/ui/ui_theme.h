#pragma once
#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Runtime appearance. The UI_* colour macros in ui.h resolve through here, so a
 * theme change needs no call-site changes -- only a screen rebuild. */
typedef enum {
    UI_C_BG, UI_C_CARD, UI_C_CARD_HI, UI_C_ACCENT, UI_C_ACCENT2,
    UI_C_DANGER, UI_C_TEXT, UI_C_MUTED, UI_C_COUNT
} ui_color_id_t;

typedef enum { UI_DENSITY_COMPACT = 0, UI_DENSITY_NORMAL = 1, UI_DENSITY_ROOMY = 2 } ui_density_t;

#define UI_THEME_CUSTOM 0xFF
#define UI_RADIUS_DESIGN 12      /* the radius the layout was drawn against */

bool        ui_theme_pixel(void);                      /* square phosphor/pixel controls */
void        ui_theme_install_widgets(lv_theme_t *base);
void        ui_theme_init(void);                       /* pull the active theme out of g_settings */
lv_color_t  ui_theme_color(ui_color_id_t id);
uint32_t    ui_theme_color_hex(ui_color_id_t id);
void        ui_theme_set_color(ui_color_id_t id, uint32_t rgb);   /* switches to custom */
int         ui_theme_radius(int design);               /* scale a design radius */
int         ui_theme_pad(int design);                  /* scale a design padding */
bool        ui_theme_tile_accent(void);                /* tint the home tiles with the accent */
bool        ui_theme_glow(void);                       /* luminous edge and brief phosphor sweep */
bool        ui_theme_scanlines(void);                  /* CRT/EL scan lines over the screen */
void        ui_theme_glow_apply(lv_obj_t *obj);        /* call after setting an object's radius */

int         ui_theme_preset_count(void);
const char *ui_theme_preset_name(int i);
void        ui_theme_set_preset(int i);
void        ui_theme_set_terminal_amber(void);
const char *ui_theme_color_name(ui_color_id_t id);

/* Re-applies the LVGL theme and rebuilds the visible screen. */
void        ui_theme_apply(void);

#ifdef __cplusplus
}
#endif
