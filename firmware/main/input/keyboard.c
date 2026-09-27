#include "keyboard.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "ime/ime.h"
#include "settings/settings.h"
#include "ui/ui.h"

static const char *TAG = "kbd";

/* Tab5 keyboard (STM32F030) on the EXT I2C bus. */
#define KB_SDA GPIO_NUM_0
#define KB_SCL GPIO_NUM_1
#define KB_INT GPIO_NUM_50
#define KB_ADDR 0x6D
#define REG_INT_CFG 0x00
#define REG_INT_STA 0x01
#define REG_EVENT_NUM 0x02
#define REG_BRIGHTNESS 0x03
#define REG_MODE 0x10
#define REG_RGB_MODE 0x11
#define REG_KEY_EVENT 0x20
#define REG_HID_EVENT 0x30
#define REG_RGB_BASE 0x60
#define REG_VERSION 0xFE

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static bool s_present = false, s_usb_present = false;
static uint8_t s_fw = 0;
static QueueHandle_t s_q = NULL;
static key_sink_t s_sink; static bool s_have_sink = false;
static void *s_focused_ta = NULL;
static uint8_t s_i2c_mods = 0;
static TickType_t s_i2c_last_key = 0;
static key_event_t s_i2c_repeat; static bool s_i2c_held = false;

/* ------------------------------------------------------------- HID map */
typedef struct { uint8_t code; char plain; char shifted; special_key_t sk; } hidmap_t;
static const hidmap_t s_hid[] = {
    {0x04,'a','A',0},{0x05,'b','B',0},{0x06,'c','C',0},{0x07,'d','D',0},{0x08,'e','E',0},{0x09,'f','F',0},
    {0x0a,'g','G',0},{0x0b,'h','H',0},{0x0c,'i','I',0},{0x0d,'j','J',0},{0x0e,'k','K',0},{0x0f,'l','L',0},
    {0x10,'m','M',0},{0x11,'n','N',0},{0x12,'o','O',0},{0x13,'p','P',0},{0x14,'q','Q',0},{0x15,'r','R',0},
    {0x16,'s','S',0},{0x17,'t','T',0},{0x18,'u','U',0},{0x19,'v','V',0},{0x1a,'w','W',0},{0x1b,'x','X',0},
    {0x1c,'y','Y',0},{0x1d,'z','Z',0},
    {0x1e,'1','!',0},{0x1f,'2','@',0},{0x20,'3','#',0},{0x21,'4','$',0},{0x22,'5','%',0},{0x23,'6','^',0},
    {0x24,'7','&',0},{0x25,'8','*',0},{0x26,'9','(',0},{0x27,'0',')',0},
    {0x28,0,0,SK_ENTER},{0x29,0,0,SK_ESC},{0x2a,0,0,SK_BACKSPACE},{0x2b,0,0,SK_TAB},{0x2c,' ',' ',0},
    {0x2d,'-','_',0},{0x2e,'=','+',0},{0x2f,'[','{',0},{0x30,']','}',0},{0x31,'\\','|',0},{0x32,'#','~',0},
    {0x33,';',':',0},{0x34,'\'','"',0},{0x35,'`','~',0},{0x36,',','<',0},{0x37,'.','>',0},{0x38,'/','?',0},
    {0x3a,0,0,SK_F1},{0x3b,0,0,SK_F2},{0x3c,0,0,SK_F3},{0x3d,0,0,SK_F4},{0x3e,0,0,SK_F5},{0x3f,0,0,SK_F6},
    {0x40,0,0,SK_F7},{0x41,0,0,SK_F8},{0x42,0,0,SK_F9},{0x43,0,0,SK_F10},{0x44,0,0,SK_F11},{0x45,0,0,SK_F12},
    {0x49,0,0,SK_INSERT},{0x4a,0,0,SK_HOME},{0x4b,0,0,SK_PGUP},{0x4c,0,0,SK_DELETE},{0x4d,0,0,SK_END},
    {0x4e,0,0,SK_PGDN},{0x4f,0,0,SK_RIGHT},{0x50,0,0,SK_LEFT},{0x51,0,0,SK_DOWN},{0x52,0,0,SK_UP},
    {0x54,'/','/',0},{0x55,'*','*',0},{0x56,'-','-',0},{0x57,'+','+',0},{0x58,0,0,SK_ENTER},
    {0x59,'1','1',0},{0x5a,'2','2',0},{0x5b,'3','3',0},{0x5c,'4','4',0},{0x5d,'5','5',0},{0x5e,'6','6',0},
    {0x5f,'7','7',0},{0x60,'8','8',0},{0x61,'9','9',0},{0x62,'0','0',0},{0x63,'.','.',0},
    {0x64,'\\','|',0},{0x67,'=','=',0},
    /* Japanese keyboards */
    {0x87,'\\','_',0},{0x89,'\\','|',0},
};

static bool hid_to_event(uint8_t mod, uint8_t code, key_event_t *ev)
{
    memset(ev, 0, sizeof(*ev));
    bool shift = mod & 0x22, ctrl = mod & 0x11, alt = mod & 0x44, meta = mod & 0x88;
    ev->mods = (ctrl ? MOD_CTRL : 0) | (shift ? MOD_SHIFT : 0) | (alt ? MOD_ALT : 0) | (meta ? MOD_META : 0);
    for (size_t i = 0; i < sizeof(s_hid) / sizeof(s_hid[0]); i++) {
        if (s_hid[i].code != code) continue;
        if (s_hid[i].sk) { ev->type = KEY_EV_SPECIAL; ev->key = s_hid[i].sk; return true; }
        ev->type = KEY_EV_CHAR;
        ev->cp = (uint8_t)(shift ? s_hid[i].shifted : s_hid[i].plain);
        return true;
    }
    return false;
}

/* --------------------------------------------------- matrix layouts */
/* In KEY mode the keyboard reports raw matrix positions instead of the HID
 * codes its own firmware resolves, so an alternative layout can be mapped
 * here and the STM32 firmware stays stock.  Register 0x20 then returns one
 * byte per event: bit 7 pressed, bits 6:4 row, bits 3:0 column, 0xFF when
 * the queue is empty. */
#define KC_SK(x)   (0x1000u | (x))
#define KC_MOD(m)  (0x2000u | (m))
#define KC_ACT(a)  (0x3000u | (a))
#define KC_TYPE(k) ((k) & 0xF000u)
#define KC_ARG(k)  ((k) & 0x0FFFu)
enum { ACT_FN = 1, ACT_CAPS };

typedef struct { uint16_t base, shift, fn; } keydef_t;

/* JIS.  The keycaps are reprinted, so every position is reassigned: the
 * symbol row becomes QWERTY, Ctrl moves onto the old Tab key, and the
 * bottom row carries the modifiers, 英数/かな and Fn. */
static const keydef_t s_jis[5][14] = {
    { {KC_SK(SK_ESC),0,0},
      {'1','!',KC_SK(SK_F1)},  {'2','"',KC_SK(SK_F2)},  {'3','#',KC_SK(SK_F3)},
      {'4','$',KC_SK(SK_F4)},  {'5','%',KC_SK(SK_F5)},  {'6','&',KC_SK(SK_F6)},
      {'7','\'',KC_SK(SK_F7)}, {'8','(',KC_SK(SK_F8)},  {'9',')',KC_SK(SK_F9)},
      {'0',0,KC_SK(SK_F10)},   {'-','=',KC_SK(SK_F11)}, {'^','~',KC_SK(SK_F12)},
      {KC_SK(SK_BACKSPACE),KC_SK(SK_DELETE),KC_SK(SK_DELETE)} },

    { {KC_SK(SK_TAB),0,0},
      {'q','Q',0},{'w','W',0},{'e','E',0},{'r','R',0},{'t','T',0},
      {'y','Y',0},{'u','U',0},{'i','I',0},{'o','O',0},{'p','P',0},
      {'@','`',0},{'[','{',0},{'\\','|',0} },

    { {KC_MOD(MOD_CTRL),0,0},
      {'a','A',0},{'s','S',0},{'d','D',0},{'f','F',0},{'g','G',0},
      {'h','H',0},{'j','J',0},{'k','K',0},{'l','L',0},
      {';','+',0},{':','*',0},{']','}',0},
      {KC_SK(SK_ENTER),0,0} },

    { {KC_MOD(MOD_SHIFT),0,0},
      {'_','_',0},
      {'z','Z',0},{'x','X',0},{'c','C',0},{'v','V',0},{'b','B',0},{'n','N',0},{'m','M',0},
      {',','<',0},{'.','>',0},{'/','?',0},
      {KC_SK(SK_UP),0,0},
      {KC_MOD(MOD_SHIFT),0,0} },

    { {KC_ACT(ACT_CAPS),0,0}, {KC_MOD(MOD_ALT),0,0}, {KC_MOD(MOD_META),0,0},
      {KC_SK(SK_IME_ASCII),0,0},
      {' ',0,0},{' ',0,0},{' ',0,0},{' ',0,0},
      {KC_SK(SK_IME_KANA),0,0},
      {KC_ACT(ACT_FN),0,0},
      {KC_MOD(MOD_META),0,0},
      {KC_SK(SK_LEFT),0,0},{KC_SK(SK_DOWN),0,0},{KC_SK(SK_RIGHT),0,0} },
};

/* NULL leaves the keyboard in HID mode, mapped by its own firmware. */
static const keydef_t (*volatile s_layout)[14] = NULL;
static volatile bool s_mode_dirty = false;
static uint8_t s_mx_mods = 0;
static bool s_mx_fn = false, s_mx_caps = false;
static int s_mx_row = -1, s_mx_col = -1;

static uint16_t layout_lookup(const keydef_t *kd)
{
    if (s_mx_fn && kd->fn) return kd->fn;
    bool upper = (s_mx_mods & MOD_SHIFT) != 0;
    if (s_mx_caps && kd->base >= 'a' && kd->base <= 'z') upper = !upper;
    return (upper && kd->shift) ? kd->shift : kd->base;
}

static void matrix_event(const keydef_t (*layout)[14], uint8_t row, uint8_t col, bool pressed)
{
    if (row >= 5 || col >= 14) return;
    const keydef_t *kd = &layout[row][col];
    if (KC_TYPE(kd->base) == 0x2000) {          /* modifier: held state only */
        if (pressed) s_mx_mods |= KC_ARG(kd->base);
        else s_mx_mods &= ~KC_ARG(kd->base);
        return;
    }
    if (KC_TYPE(kd->base) == 0x3000) {
        if (KC_ARG(kd->base) == ACT_FN) s_mx_fn = pressed;
        else if (pressed && KC_ARG(kd->base) == ACT_CAPS) s_mx_caps = !s_mx_caps;
        return;
    }
    if (!pressed) {
        if (row == s_mx_row && col == s_mx_col) s_i2c_held = false;
        return;
    }
    uint16_t kc = layout_lookup(kd);
    if (!kc) return;
    key_event_t ke;
    memset(&ke, 0, sizeof(ke));
    ke.mods = s_mx_mods;
    if (KC_TYPE(kc) == 0x1000) { ke.type = KEY_EV_SPECIAL; ke.key = KC_ARG(kc); }
    else { ke.type = KEY_EV_CHAR; ke.cp = kc; }
    s_mx_row = row; s_mx_col = col;
    s_i2c_repeat = ke; s_i2c_held = true; s_i2c_last_key = xTaskGetTickCount();
    xQueueSend(s_q, &ke, 0);
}

void keyboard_set_layout(uint8_t layout)
{
    const keydef_t (*m)[14] = layout == KBD_LAYOUT_JIS ? s_jis : NULL;
    if (m == s_layout) return;
    s_layout = m;
    s_mx_mods = 0; s_mx_fn = false; s_mx_caps = false;
    s_mx_row = s_mx_col = -1;
    s_i2c_held = false;
    if (s_present) s_mode_dirty = true;   /* the keyboard task switches modes */
}

/* --------------------------------------------------------- dispatching */
static void lvgl_sink_text(const char *utf8, void *user)
{
    (void)user;
    if (s_focused_ta) lv_textarea_add_text(s_focused_ta, utf8);
}
static bool lvgl_sink_key(const key_event_t *ev, void *user)
{
    (void)user;
    lv_obj_t *ta = s_focused_ta;
    if (!ta) return false;
    if (ev->type == KEY_EV_CHAR) {
        if (ev->mods & MOD_CTRL) return false;
        char b[5]; int n = 0; uint32_t c = ev->cp;
        if (c < 0x80) b[n++] = c;
        else if (c < 0x800) { b[n++] = 0xC0 | (c >> 6); b[n++] = 0x80 | (c & 0x3F); }
        else if (c < 0x10000) { b[n++] = 0xE0 | (c >> 12); b[n++] = 0x80 | ((c >> 6) & 0x3F); b[n++] = 0x80 | (c & 0x3F); }
        else { b[n++] = 0xF0 | (c >> 18); b[n++] = 0x80 | ((c >> 12) & 0x3F); b[n++] = 0x80 | ((c >> 6) & 0x3F); b[n++] = 0x80 | (c & 0x3F); }
        b[n] = 0;
        lv_textarea_add_text(ta, b);
        return true;
    }
    switch (ev->key) {
    case SK_BACKSPACE: lv_textarea_delete_char(ta); return true;
    case SK_DELETE: lv_textarea_delete_char_forward(ta); return true;
    case SK_LEFT: lv_textarea_cursor_left(ta); return true;
    case SK_RIGHT: lv_textarea_cursor_right(ta); return true;
    case SK_UP: lv_textarea_cursor_up(ta); return true;
    case SK_DOWN: lv_textarea_cursor_down(ta); return true;
    case SK_HOME: lv_textarea_set_cursor_pos(ta, 0); return true;
    case SK_END: lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST); return true;
    case SK_ENTER:
        if (lv_textarea_get_one_line(ta)) lv_obj_send_event(ta, LV_EVENT_READY, NULL);
        else lv_textarea_add_char(ta, '\n');
        return true;
    case SK_TAB: {
        /* move focus to next textarea sibling if any */
        lv_obj_t *p = lv_obj_get_parent(ta);
        uint32_t n = lv_obj_get_child_count(p), idx = lv_obj_get_index(ta);
        for (uint32_t i = 1; i <= n; i++) {
            lv_obj_t *c = lv_obj_get_child(p, (idx + i) % n);
            if (lv_obj_check_type(c, &lv_textarea_class)) { lv_obj_send_event(c, LV_EVENT_CLICKED, NULL); keyboard_lvgl_focus(c); break; }
        }
        return true;
    }
    default: return false;
    }
}

void keyboard_lvgl_focus(void *ta) { s_focused_ta = ta; }
void *keyboard_lvgl_focused(void) { return s_focused_ta; }

void keyboard_set_sink(const key_sink_t *sink)
{
    if (sink) { s_sink = *sink; s_have_sink = true; }
    else s_have_sink = false;
    ime_reset();
}

static void sink_text(const char *utf8)
{
    if (s_have_sink && !ui_keyboard_owns_input()) { if (s_sink.text) s_sink.text(utf8, s_sink.user); }
    else lvgl_sink_text(utf8, NULL);
}
static bool sink_key(const key_event_t *ev)
{
    if (s_have_sink && !ui_keyboard_owns_input()) return s_sink.key ? s_sink.key(ev, s_sink.user) : false;
    return lvgl_sink_key(ev, NULL);
}

static void ime_commit_cb(const char *utf8, void *user) { (void)user; sink_text(utf8); }

/* The keys that pick the input source, wherever the focus is. */
static bool ime_source_key(const key_event_t *ev)
{
    if (ev->type != KEY_EV_SPECIAL) return false;
    switch (ev->key) {
    case SK_IME_TOGGLE: ime_toggle_source(); return true;
    case SK_KANA_MODE:  ime_toggle_ja_mode(); return true;
    case SK_IME_ASCII:
        if (ime_get_source() != IME_SRC_EN) ime_set_source(IME_SRC_EN);
        return true;
    case SK_IME_KANA:
        if (ime_get_source() != IME_SRC_JA) ime_set_source(IME_SRC_JA);
        else ime_toggle_ja_mode();
        return true;
    default: return false;
    }
}

static void dispatch_input(const key_event_t *ev)
{
    /* global: Ctrl+Space toggles the input source */
    if (ev->type == KEY_EV_CHAR && ev->cp == ' ' && (ev->mods & MOD_CTRL)) { ime_toggle_source(); return; }
    if (ime_source_key(ev)) return;
    if (ime_feed_key(ev)) return;
    sink_key(ev);
}

void keyboard_dispatch(const key_event_t *ev)
{
    if (ime_source_key(ev)) return;   /* never routed to the UI */
    if (!ui_keyboard_route(ev, s_have_sink)) dispatch_input(ev);
}

/* On-screen keys already have a text destination; do not navigate the UI. */
void keyboard_inject(const key_event_t *ev) { dispatch_input(ev); }

/* ------------------------------------------------------- I2C keyboard */
static bool kb_write(uint8_t reg, uint8_t val)
{
    uint8_t w[2] = { reg, val };
    return s_dev && i2c_master_transmit(s_dev, w, 2, 50) == ESP_OK;
}
static bool kb_read(uint8_t reg, uint8_t *buf, size_t n)
{
    return s_dev && i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 50) == ESP_OK;
}

static bool kb_probe(void)
{
    if (!s_bus) return false;
    if (i2c_master_probe(s_bus, KB_ADDR, 30) != ESP_OK) return false;
    if (!s_dev) {
        i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = KB_ADDR, .scl_speed_hz = 100000 };
        if (i2c_master_bus_add_device(s_bus, &dc, &s_dev) != ESP_OK) return false;
    }
    uint8_t v = 0;
    if (!kb_read(REG_VERSION, &v, 1)) return false;
    s_fw = v;
    kb_write(REG_MODE, s_layout ? 0 : 1);        /* raw matrix, or HID */
    kb_write(REG_INT_CFG, s_layout ? 0x01 : 0x02);
    s_mode_dirty = false;
    kb_write(REG_EVENT_NUM, 0);     /* clear FIFO */
    keyboard_set_led(g_settings.kbd_led_mode, g_settings.kbd_led_bright,
                     g_settings.kbd_led_rgb[0], g_settings.kbd_led_rgb[1], g_settings.kbd_led_rgb[2]);
    return true;
}

static void kb_task(void *arg)
{
    (void)arg;
    TickType_t last_probe = 0;
    int fail = 0;
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        if (!s_present) {
            if (now - last_probe > pdMS_TO_TICKS(1500)) {
                last_probe = now;
                if (kb_probe()) { s_present = true; fail = 0; ESP_LOGI(TAG, "Tab5 keyboard detected, fw=0x%02X", s_fw); }
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (s_mode_dirty) {
            s_mode_dirty = false;
            kb_write(REG_MODE, s_layout ? 0 : 1);
            kb_write(REG_INT_CFG, s_layout ? 0x01 : 0x02);
            kb_write(REG_EVENT_NUM, 0);          /* the queued events are stale */
        }
        bool has_int = gpio_get_level(KB_INT) == 0;
        uint8_t cnt = 0;
        if (!kb_read(REG_EVENT_NUM, &cnt, 1)) {
            if (++fail > 5) { s_present = false; s_i2c_held = false; ESP_LOGW(TAG, "keyboard lost"); }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        fail = 0;
        while (cnt-- > 0) {
            const keydef_t (*layout)[14] = s_layout;   /* may change under us */
            if (layout) {
                uint8_t k;
                if (!kb_read(REG_KEY_EVENT, &k, 1)) break;
                if (k == 0xFF) break;
                matrix_event(layout, (k >> 4) & 0x07, k & 0x0F, (k & 0x80) != 0);
                continue;
            }
            uint8_t ev[2];
            if (!kb_read(REG_HID_EVENT, ev, 2)) break;
            if (ev[0] == 0xFF && ev[1] == 0xFF) break;
            if (ev[1] == 0) { s_i2c_held = false; continue; }       /* release */
            key_event_t ke;
            if (!hid_to_event(ev[0], ev[1], &ke)) continue;
            s_i2c_repeat = ke; s_i2c_held = true; s_i2c_last_key = xTaskGetTickCount();
            xQueueSend(s_q, &ke, 0);
        }
        if (has_int) kb_write(REG_INT_STA, 0);
        /* simple auto-repeat while held */
        if (s_i2c_held && xTaskGetTickCount() - s_i2c_last_key > pdMS_TO_TICKS(500)) {
            xQueueSend(s_q, &s_i2c_repeat, 0);
            s_i2c_last_key = xTaskGetTickCount() - pdMS_TO_TICKS(440);
        }
        vTaskDelay(pdMS_TO_TICKS(has_int ? 5 : 20));
    }
}

void keyboard_set_led(uint8_t mode, uint8_t bright, uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_present && !s_dev) return;
    kb_write(REG_BRIGHTNESS, bright > 100 ? 100 : bright);
    kb_write(REG_RGB_MODE, mode ? 1 : 0);
    if (mode) {
        uint8_t w[9] = { REG_RGB_BASE, 0, r, g, b, 0, r, g, b };
        if (s_dev) i2c_master_transmit(s_dev, w, 9, 50);
    }
}

/* ---------------------------------------------------------- USB HID */
static uint8_t s_usb_prev[6];
void keyboard_usb_hid_report(const uint8_t *rep, int len)
{
    if (len < 8) return;
    s_usb_present = true;
    uint8_t mod = rep[0];
    for (int i = 2; i < 8; i++) {
        uint8_t code = rep[i];
        if (!code) continue;
        bool was = false;
        for (int j = 0; j < 6; j++) if (s_usb_prev[j] == code) was = true;
        if (was) continue;
        key_event_t ke;
        if (hid_to_event(mod, code, &ke)) xQueueSend(s_q, &ke, 0);
    }
    memcpy(s_usb_prev, rep + 2, 6);
}

/* ------------------------------------------------------- LVGL bridge */
static void drain_timer_cb(lv_timer_t *t)
{
    (void)t;
    key_event_t ke;
    int n = 0;
    while (n++ < 16 && xQueueReceive(s_q, &ke, 0) == pdTRUE) {
        lv_display_trigger_activity(NULL);
        keyboard_dispatch(&ke);
    }
}

void keyboard_init(void)
{
    s_q = xQueueCreate(64, sizeof(key_event_t));
    keyboard_set_layout(g_settings.kbd_layout);
    ime_set_commit_cb(ime_commit_cb, NULL);
    gpio_config_t io = { .pin_bit_mask = 1ULL << KB_INT, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&io);
    i2c_master_bus_config_t bc = {
        .i2c_port = 0, .sda_io_num = KB_SDA, .scl_io_num = KB_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bc, &s_bus) != ESP_OK) {
        ESP_LOGW(TAG, "EXT I2C bus init failed");
    } else {
        ESP_LOGI(TAG, "created keyboard i2c bus port=0 sda=%d scl=%d addr=0x%02X", KB_SDA, KB_SCL, KB_ADDR);
        if (kb_probe()) { s_present = true; ESP_LOGI(TAG, "Tab5 keyboard detected, fw=0x%02X", s_fw); }
        else ESP_LOGI(TAG, "startup keyboard probe result: absent");
    }
    xTaskCreatePinnedToCore(kb_task, "kbd", 4096, NULL, 6, NULL, 1);
    lv_timer_create(drain_timer_cb, 10, NULL);
}

bool keyboard_present(void) { return s_present; }
bool keyboard_usb_present(void) { return s_usb_present; }
bool keyboard_any_present(void) { return s_present || s_usb_present; }
uint8_t keyboard_fw_version(void) { return s_fw; }
