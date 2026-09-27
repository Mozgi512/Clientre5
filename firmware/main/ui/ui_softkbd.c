#include "ui_softkbd.h"
#include "ui.h"
#include "input/keyboard.h"
#include "ime/ime.h"
#include "ime/ime_ui.h"
#include <string.h>

#define KBD_H 300

typedef struct { const char *label; int type; uint32_t cp; special_key_t sk; int w; } key_def_t;
enum { KT_CHAR, KT_SPECIAL, KT_SHIFT, KT_SYM, KT_IME, KT_HIDE, KT_CTRL, KT_ALT };

static const char *ROWS_LOWER[4] = { "qwertyuiop", "asdfghjkl", "zxcvbnm", "" };
static const char *ROWS_UPPER[4] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM", "" };
static const char *ROWS_SYM[4]   = { "1234567890", "-/:;()$&@\"", ".,?!'~`|", "" };
static const char *ROWS_SYM2[4]  = { "[]{}#%^*+=", "_\\<>€£¥•", ".,?!'~`|", "" };

typedef struct {
    lv_obj_t *root, *rows[4];
    bool shift, sym, sym2, ctrl, alt;
    lv_obj_t *ime_btn, *shift_btn, *sym_btn, *ctrl_btn, *alt_btn;
} kbd_t;

static kbd_t *s_kbd = NULL;
static lv_obj_t *s_attached_ta = NULL;

static void rebuild(kbd_t *k);

static void send_char(kbd_t *k, uint32_t cp)
{
    key_event_t ev = { .type = KEY_EV_CHAR, .cp = cp, .mods = (k->ctrl ? MOD_CTRL : 0) | (k->alt ? MOD_ALT : 0) };
    keyboard_inject(&ev);
    if (k->ctrl || k->alt || (k->shift && !k->sym)) { k->ctrl = k->alt = false; if (k->shift) k->shift = false; rebuild(k); }
}
static void send_special(kbd_t *k, special_key_t sk)
{
    key_event_t ev = { .type = KEY_EV_SPECIAL, .key = sk, .mods = (k->ctrl ? MOD_CTRL : 0) | (k->alt ? MOD_ALT : 0) };
    keyboard_inject(&ev);
    if (k->ctrl || k->alt) { k->ctrl = k->alt = false; rebuild(k); }
}

static void key_cb(lv_event_t *e)
{
    kbd_t *k = s_kbd;
    lv_obj_t *b = lv_event_get_target_obj(e);
    const key_def_t *d = lv_obj_get_user_data(b);
    switch (d->type) {
    case KT_CHAR: send_char(k, d->cp); break;
    case KT_SPECIAL: send_special(k, d->sk); break;
    case KT_SHIFT: if (k->sym) { k->sym2 = !k->sym2; } else k->shift = !k->shift; rebuild(k); break;
    case KT_SYM: k->sym = !k->sym; k->sym2 = false; rebuild(k); break;
    case KT_IME: ime_toggle_source(); rebuild(k); break;
    case KT_HIDE: softkbd_hide_all(); break;
    case KT_CTRL: k->ctrl = !k->ctrl; rebuild(k); break;
    case KT_ALT: k->alt = !k->alt; rebuild(k); break;
    }
}

static lv_obj_t *mk_key(lv_obj_t *row, const char *label, int type, uint32_t cp, special_key_t sk, int grow, bool active)
{
    key_def_t *d = lv_malloc(sizeof(key_def_t));
    d->label = label; d->type = type; d->cp = cp; d->sk = sk; d->w = grow;
    lv_obj_t *b = lv_button_create(row);
    lv_obj_set_height(b, LV_PCT(100));
    lv_obj_set_flex_grow(b, grow);
    lv_obj_set_style_radius(b, ui_theme_radius(8), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, ui_theme_pad(0), 0);
    lv_obj_set_style_bg_color(b, active ? UI_ACCENT : (type == KT_CHAR ? UI_CARD_HI : UI_CARD), 0);
    lv_obj_set_style_bg_color(b, UI_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b, active && ui_theme_pixel() ? UI_BG : UI_TEXT, 0);
    lv_obj_set_style_text_color(b, ui_theme_pixel() ? UI_BG : UI_TEXT, LV_STATE_PRESSED);
    lv_obj_set_user_data(b, d);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, FONT_BODY, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, key_cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static void free_keys(lv_obj_t *row)
{
    uint32_t n = lv_obj_get_child_count(row);
    for (uint32_t i = 0; i < n; i++) { void *d = lv_obj_get_user_data(lv_obj_get_child(row, i)); if (d) lv_free(d); }
    lv_obj_clean(row);
}

static void rebuild(kbd_t *k)
{
    const char **rows = k->sym ? (k->sym2 ? ROWS_SYM2 : ROWS_SYM) : (k->shift ? ROWS_UPPER : ROWS_LOWER);
    static char labels[3][12][8];
    for (int r = 0; r < 3; r++) {
        free_keys(k->rows[r]);
        const char *s = rows[r];
        int i = 0;
        if (r == 1 && !k->sym) { lv_obj_t *sp = lv_obj_create(k->rows[r]); lv_obj_set_size(sp, 4, 4); lv_obj_set_style_bg_opa(sp, 0, 0); lv_obj_set_style_border_width(sp, 0, 0); lv_obj_set_flex_grow(sp, 1); }
        if (r == 2) mk_key(k->rows[r], k->sym ? (k->sym2 ? "1/2" : "2/2") : LV_SYMBOL_UP, KT_SHIFT, 0, 0, 3, k->shift && !k->sym);
        while (*s && i < 12) {
            uint32_t cp; int n;
            unsigned char c = (unsigned char)*s;
            if (c < 0x80) { cp = c; n = 1; }
            else if ((c & 0xE0) == 0xC0) { cp = ((c & 0x1F) << 6) | (s[1] & 0x3F); n = 2; }
            else { cp = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); n = 3; }
            memcpy(labels[r][i], s, n); labels[r][i][n] = 0;
            mk_key(k->rows[r], labels[r][i], KT_CHAR, cp, 0, 2, false);
            s += n; i++;
        }
        if (r == 1 && !k->sym) { lv_obj_t *sp = lv_obj_create(k->rows[r]); lv_obj_set_size(sp, 4, 4); lv_obj_set_style_bg_opa(sp, 0, 0); lv_obj_set_style_border_width(sp, 0, 0); lv_obj_set_flex_grow(sp, 1); }
        if (r == 2) mk_key(k->rows[r], LV_SYMBOL_BACKSPACE, KT_SPECIAL, 0, SK_BACKSPACE, 3, false);
    }
    free_keys(k->rows[3]);
    mk_key(k->rows[3], k->sym ? "abc" : "?123", KT_SYM, 0, 0, 3, false);
    mk_key(k->rows[3], ime_status_label(), KT_IME, 0, 0, 2, ime_is_active());
    mk_key(k->rows[3], "Ctrl", KT_CTRL, 0, 0, 2, k->ctrl);
    mk_key(k->rows[3], "Alt", KT_ALT, 0, 0, 2, k->alt);
    mk_key(k->rows[3], " ", KT_CHAR, ' ', 0, 8, false);
    mk_key(k->rows[3], LV_SYMBOL_LEFT, KT_SPECIAL, 0, SK_LEFT, 2, false);
    mk_key(k->rows[3], LV_SYMBOL_RIGHT, KT_SPECIAL, 0, SK_RIGHT, 2, false);
    mk_key(k->rows[3], LV_SYMBOL_NEW_LINE, KT_SPECIAL, 0, SK_ENTER, 3, false);
    mk_key(k->rows[3], LV_SYMBOL_DOWN, KT_HIDE, 0, 0, 2, false);
}

static void kbd_delete_cb(lv_event_t *e)
{
    (void)e;
    if (s_kbd) { for (int r = 0; r < 4; r++) free_keys(s_kbd->rows[r]); lv_free(s_kbd); s_kbd = NULL; }
    ime_ui_set_anchor(-1);
}

lv_obj_t *softkbd_create(lv_obj_t *parent)
{
    if (s_kbd) lv_obj_delete(s_kbd->root);
    kbd_t *k = lv_calloc(1, sizeof(kbd_t));
    s_kbd = k;
    k->root = lv_obj_create(parent);
    lv_obj_set_size(k->root, LV_PCT(100), KBD_H);
    lv_obj_align(k->root, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(k->root, UI_CARD, 0);
    lv_obj_set_style_bg_opa(k->root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(k->root, 0, 0);
    lv_obj_set_style_radius(k->root, 0, 0);
    lv_obj_set_style_pad_all(k->root, 6, 0);
    lv_obj_set_style_pad_row(k->root, 6, 0);
    lv_obj_set_flex_flow(k->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(k->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(k->root, UI_KEYBOARD_SKIP);
    lv_obj_add_event_cb(k->root, kbd_delete_cb, LV_EVENT_DELETE, NULL);
    for (int r = 0; r < 4; r++) {
        lv_obj_t *row = lv_obj_create(k->root);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_flex_grow(row, 1);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, ui_theme_pad(0), 0);
        lv_obj_set_style_pad_column(row, ui_theme_pad(5), 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        k->rows[r] = row;
    }
    rebuild(k);
    lv_obj_update_layout(k->root);
    lv_area_t a; lv_obj_get_coords(k->root, &a);
    ime_ui_set_anchor(a.y1);
    return k->root;
}

int softkbd_height(void) { return KBD_H; }
bool softkbd_visible(void) { return s_kbd && !lv_obj_has_flag(s_kbd->root, LV_OBJ_FLAG_HIDDEN); }

void softkbd_hide_all(void)
{
    if (s_kbd) lv_obj_delete(s_kbd->root);
    s_kbd = NULL;
    ime_reset();
}

/* ---- text area integration ---- */
static void ta_event_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target_obj(e);
    lv_obj_t *parent = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        keyboard_lvgl_focus(ta);
        s_attached_ta = ta;
        if (!keyboard_any_present() && !softkbd_visible()) softkbd_create(parent);
        /* make sure the text area is visible above the keyboard */
        lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
    } else if (code == LV_EVENT_READY || code == LV_EVENT_DEFOCUSED) {
        if (code == LV_EVENT_READY) { ime_commit_preedit(); softkbd_hide_all(); }
        if (code == LV_EVENT_DEFOCUSED && keyboard_lvgl_focused() == ta) keyboard_lvgl_focus(NULL);
    } else if (code == LV_EVENT_DELETE) {
        if (keyboard_lvgl_focused() == ta) keyboard_lvgl_focus(NULL);
        if (s_attached_ta == ta) { s_attached_ta = NULL; softkbd_hide_all(); }
    }
}

void softkbd_attach_textarea(lv_obj_t *ta, lv_obj_t *parent)
{
    lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, parent);
}
