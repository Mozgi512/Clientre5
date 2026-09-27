#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { KEY_EV_CHAR = 0, KEY_EV_SPECIAL } key_ev_type_t;
typedef enum {
    SK_NONE = 0, SK_ENTER, SK_ESC, SK_BACKSPACE, SK_TAB, SK_UP, SK_DOWN, SK_LEFT, SK_RIGHT,
    SK_HOME, SK_END, SK_PGUP, SK_PGDN, SK_INSERT, SK_DELETE,
    SK_F1, SK_F2, SK_F3, SK_F4, SK_F5, SK_F6, SK_F7, SK_F8, SK_F9, SK_F10, SK_F11, SK_F12,
    SK_IME_TOGGLE,      /* soft keyboard "A/あ" key */
    SK_KANA_MODE,       /* hiragana <-> katakana */
    SK_IME_ASCII,       /* JIS "英数": force the ASCII source */
    SK_IME_KANA,        /* JIS "かな": force Japanese, then hiragana <-> katakana */
} special_key_t;

/* Physical layout of the Tab5 keyboard.  STOCK leaves the mapping to the
 * keyboard's own firmware (what the keycaps say); the others read the raw
 * matrix and resolve the layout here. */
typedef enum { KBD_LAYOUT_STOCK = 0, KBD_LAYOUT_JIS = 1, KBD_LAYOUT_COUNT } kbd_layout_t;

#define MOD_CTRL  0x01
#define MOD_SHIFT 0x02
#define MOD_ALT   0x04
#define MOD_META  0x08

typedef struct {
    key_ev_type_t type;
    uint32_t cp;          /* unicode codepoint for KEY_EV_CHAR */
    special_key_t key;    /* for KEY_EV_SPECIAL */
    uint8_t mods;
} key_event_t;

/* A sink receives keys that the IME did not consume, and committed text. */
typedef struct {
    bool (*key)(const key_event_t *ev, void *user);   /* return true if handled */
    void (*text)(const char *utf8, void *user);       /* committed text (IME or plain chars) */
    void *user;
} key_sink_t;

void keyboard_init(void);
bool keyboard_present(void);        /* Tab5 I2C keyboard */
bool keyboard_usb_present(void);    /* USB HID keyboard */
bool keyboard_any_present(void);
uint8_t keyboard_fw_version(void);
void keyboard_set_sink(const key_sink_t *sink);   /* NULL -> default LVGL sink */
void keyboard_inject(const key_event_t *ev);      /* from soft keyboard / tests (LVGL task) */
void keyboard_dispatch(const key_event_t *ev);    /* IME -> sink pipeline (LVGL task) */
void keyboard_set_layout(uint8_t layout);          /* kbd_layout_t */
void keyboard_set_led(uint8_t mode, uint8_t bright, uint8_t r, uint8_t g, uint8_t b);
void keyboard_usb_hid_report(const uint8_t *report, int len); /* called by USB HID glue */
/* The default LVGL sink targets the focused text area. */
void keyboard_lvgl_focus(void *lv_obj_textarea);   /* NULL to clear */
void *keyboard_lvgl_focused(void);

#ifdef __cplusplus
}
#endif
