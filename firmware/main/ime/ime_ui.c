#include "ime_ui.h"
#include "ime.h"
#include "ui/ui.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_bar = NULL, *s_pre = NULL, *s_cands = NULL, *s_indicator = NULL;
static int32_t s_anchor = -1;

static void cand_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ime_select_candidate(idx);
}

static void ensure_bar(void)
{
    if (s_bar) return;
    s_bar = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_bar, UI_CARD, 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_bar, UI_ACCENT, 0);
    lv_obj_set_style_border_width(s_bar, 1, 0);
    lv_obj_set_style_border_side(s_bar, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(s_bar, ui_theme_radius(0), 0);
    lv_obj_set_style_pad_all(s_bar, ui_theme_pad(6), 0);
    lv_obj_set_style_pad_row(s_bar, ui_theme_pad(4), 0);
    lv_obj_set_flex_flow(s_bar, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);
    s_pre = lv_label_create(s_bar);
    lv_obj_set_style_text_font(s_pre, FONT_BODY, 0);
    lv_obj_set_style_text_color(s_pre, UI_ACCENT2, 0);
    lv_obj_set_style_text_decor(s_pre, LV_TEXT_DECOR_UNDERLINE, 0);
    s_cands = lv_obj_create(s_bar);
    lv_obj_set_size(s_cands, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_cands, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cands, 0, 0);
    lv_obj_set_style_pad_all(s_cands, ui_theme_pad(0), 0);
    lv_obj_set_style_pad_column(s_cands, ui_theme_pad(6), 0);
    lv_obj_set_flex_flow(s_cands, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(s_cands, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_cands, LV_SCROLLBAR_MODE_OFF);
}

static void place_bar(void)
{
    lv_obj_update_layout(s_bar);
    int32_t h = lv_obj_get_height(s_bar);
    int32_t bottom = s_anchor < 0 ? lv_display_get_vertical_resolution(NULL) : s_anchor;
    lv_obj_set_pos(s_bar, 0, bottom - h);
}

void ime_ui_set_anchor(int32_t bottom_y) { s_anchor = bottom_y; if (s_bar) place_bar(); }
void ime_ui_set_indicator(lv_obj_t *label) { s_indicator = label; }

void ime_ui_refresh(void)
{
    if (s_indicator) lv_label_set_text(s_indicator, ime_status_label());
    if (!ime_is_composing()) { ime_ui_hide(); return; }
    ensure_bar();
    lv_label_set_text(s_pre, ime_preedit());
    lv_obj_clean(s_cands);
    int n = ime_candidate_count();
    int sel = ime_candidate_index();
    int page = sel >= 0 ? (sel / IME_PAGE) * IME_PAGE : 0;
    if (n == 0) {
        lv_obj_t *hint = lv_label_create(s_cands);
        lv_label_set_text(hint, ime_get_source() == IME_SRC_ZH ? "Space" : "Space: 変換  Enter: 確定");
        lv_obj_set_style_text_color(hint, UI_MUTED, 0);
        lv_obj_set_style_text_font(hint, FONT_SMALL, 0);
    }
    for (int i = page; i < n && i < page + IME_PAGE; i++) {
        lv_obj_t *b = lv_button_create(s_cands);
        lv_obj_set_height(b, 40);
        lv_obj_set_style_pad_hor(b, ui_theme_pad(10), 0);
        lv_obj_set_style_radius(b, ui_theme_radius(8), 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, i == sel ? UI_ACCENT : UI_CARD_HI, 0);
        lv_obj_set_style_text_color(b, i == sel && ui_theme_pixel() ? UI_BG : UI_TEXT, 0);
        lv_obj_t *l = lv_label_create(b);
        char txt[112]; snprintf(txt, sizeof(txt), "%d %s", i - page + 1, ime_candidate(i));
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_font(l, FONT_BODY, 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, cand_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    if (n > page + IME_PAGE) {
        lv_obj_t *more = lv_label_create(s_cands);
        lv_label_set_text(more, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(more, UI_MUTED, 0);
    }
    lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    place_bar();
}

void ime_ui_hide(void)
{
    if (s_bar) lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
}

static void update_cb(void *user) { (void)user; ime_ui_refresh(); }

void ime_ui_init(void)
{
    ime_set_update_cb(update_cb, NULL);
}
