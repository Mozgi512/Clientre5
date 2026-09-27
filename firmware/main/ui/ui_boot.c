#include "ui_boot.h"
#include "fonts/fonts.h"

#define BOOT_BG     0x000000
#define BOOT_ACCENT 0xFF8C00
#define BOOT_TEXT   0xFFA836
#define BOOT_MUTED  0x9A5F14

/* A terminal monogram, stored as 32 bytes rather than a decoded splash image. */
static const uint16_t s_mark[16] = {
    0x0000, 0x7FFE, 0x4002, 0x4002,
    0x4802, 0x4402, 0x4202, 0x4402,
    0x4872, 0x4002, 0x4002, 0x7FFE,
    0x0000, 0x0180, 0x07E0, 0x0000
};

static void mark_draw(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_area_t bounds;
    lv_obj_get_coords(obj, &bounds);
    int pixel = lv_obj_get_width(obj) / 16;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(BOOT_ACCENT);
    d.bg_opa = LV_OPA_COVER;
    lv_layer_t *layer = lv_event_get_layer(e);
    /* Consecutive lit pixels become one rectangle. No blur/alpha/image decode. */
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16;) {
            if (!(s_mark[y] & (0x8000u >> x))) { x++; continue; }
            int start = x++;
            while (x < 16 && (s_mark[y] & (0x8000u >> x))) x++;
            lv_area_t a = { bounds.x1 + start * pixel, bounds.y1 + y * pixel,
                            bounds.x1 + x * pixel - 1, bounds.y1 + (y + 1) * pixel - 1 };
            lv_draw_rect(layer, &d, &a);
        }
    }
}

static void boot_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                       uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
}

void ui_boot_show(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, lv_display_get_horizontal_resolution(NULL),
                            lv_display_get_vertical_resolution(NULL));
    lv_obj_set_style_bg_color(screen, lv_color_hex(BOOT_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *group = lv_obj_create(screen);
    lv_obj_remove_style_all(group);
    lv_obj_set_size(group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(group, 16, 0);
    lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *mark = lv_obj_create(group);
    lv_obj_remove_style_all(mark);
    lv_obj_set_size(mark, 128, 128);
    lv_obj_remove_flag(mark, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mark, mark_draw, LV_EVENT_DRAW_MAIN, NULL);
    /* Const fonts can be used before their runtime fallback copies exist. */
    boot_label(group, "Clientre 5", &font_px36b, BOOT_TEXT);
    boot_label(group, "STARTING SYSTEM", &font_px20, BOOT_MUTED);
    lv_obj_center(group);

    lv_obj_t *previous = lv_screen_active();
    lv_screen_load(screen);
    if (previous && previous != screen) lv_obj_delete(previous);
    /* Ensure the logo has actually been submitted before slow SD/IME work.
     * There is deliberately no sleep, refresh timer or minimum display time. */
    lv_refr_now(NULL);
}
