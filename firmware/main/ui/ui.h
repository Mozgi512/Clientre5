#pragma once
#include "lvgl.h"
#include "i18n/i18n.h"
#include "fonts/fonts.h"
#include "ui/ui_theme.h"
#include <stdbool.h>
#include "input/keyboard.h"
#ifdef __cplusplus
extern "C" {
#endif

/* ---- colours ----
 * These resolve through the runtime theme, so every widget follows a theme
 * change without touching its call site. */
#define UI_BG          ui_theme_color(UI_C_BG)
#define UI_CARD        ui_theme_color(UI_C_CARD)
#define UI_CARD_HI     ui_theme_color(UI_C_CARD_HI)
#define UI_ACCENT      ui_theme_color(UI_C_ACCENT)
#define UI_ACCENT2     ui_theme_color(UI_C_ACCENT2)
#define UI_DANGER      ui_theme_color(UI_C_DANGER)
#define UI_WARN        UI_ACCENT2
#define UI_TEXT        ui_theme_color(UI_C_TEXT)
#define UI_MUTED       ui_theme_color(UI_C_MUTED)
#define UI_TOPBAR_H    56
/* Exclude a touch-only subtree from physical keyboard navigation. */
#define UI_KEYBOARD_SKIP LV_OBJ_FLAG_USER_4

/* ---- screen stack ---- */
typedef lv_obj_t *(*ui_screen_create_fn)(void *arg);
void ui_init(void);
bool ui_keyboard_route(const key_event_t *ev, bool terminal);
bool ui_keyboard_owns_input(void);
void ui_theme_reinit_lvgl(void);   /* re-applies the LVGL theme after a colour change */

/* Screen gestures. LVGL only walks up from the touched widget while every
 * object in between has LV_OBJ_FLAG_GESTURE_BUBBLE, which is brittle across
 * cards, lists and buttons, so gestures are taken from the input device and
 * dispatched by active screen instead.
 * Without a handler a screen gets the default: swipe right anywhere goes back.
 * A screen that wants the horizontal axis for itself installs its own. */
typedef void (*ui_gesture_cb_t)(lv_dir_t dir);
void ui_screen_set_gesture(lv_obj_t *scr, ui_gesture_cb_t cb);
void ui_nav_push(ui_screen_create_fn create, void *arg);
void ui_nav_replace(ui_screen_create_fn create, void *arg);
void ui_nav_back(void);
void ui_nav_home(void);
bool ui_nav_is_top(ui_screen_create_fn create);
lv_obj_t *ui_nav_current(void);
void ui_nav_refresh(void);   /* recreate current screen (e.g. after language change) */

/* ---- common widgets ---- */
lv_obj_t *ui_screen_base(void);
lv_obj_t *ui_topbar(lv_obj_t *parent, const char *title, bool back_button);
lv_obj_t *ui_topbar_add_button(lv_obj_t *topbar, const char *symbol, lv_event_cb_t cb, void *user_data);
lv_obj_t *ui_content(lv_obj_t *parent);  /* scrollable column below the top bar */
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data);
lv_obj_t *ui_button_colored(lv_obj_t *parent, const char *text, lv_color_t color, lv_event_cb_t cb, void *user_data);
lv_obj_t *ui_card(lv_obj_t *parent);
lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color);
lv_obj_t *ui_row(lv_obj_t *parent);
lv_obj_t *ui_textarea(lv_obj_t *parent, const char *placeholder, bool password, bool oneline);
lv_obj_t *ui_setting_row(lv_obj_t *parent, const char *label, const char *value, lv_event_cb_t cb, void *user_data);
lv_obj_t *ui_switch_row(lv_obj_t *parent, const char *label, bool on, lv_event_cb_t cb, void *user_data);
lv_obj_t *ui_section_title(lv_obj_t *parent, const char *text);
lv_obj_t *ui_list_item(lv_obj_t *parent, const char *icon, const char *title, const char *subtitle, lv_event_cb_t cb, void *user_data);

void ui_toast(const char *text);
void ui_toastf(const char *fmt, ...);
typedef void (*ui_confirm_cb_t)(bool yes, void *user_data);
void ui_confirm(const char *title, const char *text, ui_confirm_cb_t cb, void *user_data);
void ui_alert(const char *title, const char *text);
typedef void (*ui_prompt_cb_t)(const char *text, void *user_data);
void ui_prompt(const char *title, const char *placeholder, const char *initial, bool password, ui_prompt_cb_t cb, void *user_data);
typedef void (*ui_menu_cb_t)(int index, void *user_data);
void ui_menu(const char *title, const char *const *items, int count, ui_menu_cb_t cb, void *user_data);
lv_obj_t *ui_busy(const char *text);      /* returns overlay; delete to dismiss */
void ui_busy_close(lv_obj_t *busy);

/* Status strip content used by home & screensaver. */
void ui_status_text(char *buf, size_t len);
/* Adds the WiFi/Tailscale icons, IP/battery/keyboard text and the clock to a
 * top bar, honouring g_settings.status_items. Owns its own refresh timer. */
void ui_statusbar_attach(lv_obj_t *bar);

/* The Tailscale mark is a 3x3 grid of dots and no font shipped here has it, so
 * it is drawn. `dot` is the diameter of one dot and `gap` the space between. */
lv_obj_t *ui_tailscale_icon(lv_obj_t *parent, int dot, int gap);
void ui_tailscale_icon_set_color(lv_obj_t *icon, lv_color_t color, lv_opa_t opa);

/* Utilities */
void ui_format_size(char *buf, size_t len, uint64_t bytes);
void ui_format_duration(char *buf, size_t len, uint32_t seconds);
const char *ui_basename(const char *path);
void ui_lock(void);
void ui_unlock(void);
/* Run fn on the LVGL task (safe from other tasks). */
void ui_async(lv_async_cb_t fn, void *arg);
/* Delete the timer automatically when obj (usually the screen) is deleted. */
void ui_timer_bind(lv_timer_t *t, lv_obj_t *obj);

/* Screens */
lv_obj_t *ui_lang_create(void *arg);
lv_obj_t *ui_home_create(void *arg);
lv_obj_t *ui_servers_create(void *arg);
lv_obj_t *ui_server_form_create(void *arg);      /* arg: int index or -1 */
lv_obj_t *ui_terminal_create(void *arg);         /* arg: server index */
lv_obj_t *ui_files_create(void *arg);            /* arg: const char *path or NULL */
lv_obj_t *ui_image_create(void *arg);            /* arg: const char *path */
lv_obj_t *ui_text_create(void *arg);
lv_obj_t *ui_music_create(void *arg);
lv_obj_t *ui_camera_create(void *arg);
lv_obj_t *ui_wifi_create(void *arg);
lv_obj_t *ui_settings_create(void *arg);
lv_obj_t *ui_appearance_create(void *arg);
lv_obj_t *ui_ansi_create(void *arg);
lv_obj_t *ui_ota_create(void *arg);
lv_obj_t *ui_devinfo_create(void *arg);
lv_obj_t *ui_usb_create(void *arg);
lv_obj_t *ui_webfm_create(void *arg);
lv_obj_t *ui_backup_create(void *arg);
lv_obj_t *ui_tailscale_create(void *arg);
lv_obj_t *ui_screensaver_settings_create(void *arg);
void ui_screensaver_start(void);
bool ui_screensaver_active(void);
void ui_music_bar_update(void);   /* background playback bar */

#ifdef __cplusplus
}
#endif
