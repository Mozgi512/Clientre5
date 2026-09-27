#include "term_view.h"
#include "settings/settings.h"
#include <stdlib.h>
#include <string.h>

#define TERM_DRAG_SLOP 8

struct term_view {
    lv_obj_t *obj;
    vt_t *vt;
    const lv_font_t *font;
    int cell_w, cell_h;
    lv_color_t fg, bg;
    uint32_t blink_tick; bool blink_on;
    lv_timer_t *blink_timer;
    lv_point_t press; int press_view; bool dragging;
    term_cursor_t cursor_style; bool cursor_blink; lv_color_t cursor_col; bool cursor_col_set;
};

static uint32_t utf8_put(uint32_t c, char *b)
{
    int n = 0;
    if (c < 0x80) b[n++] = c;
    else if (c < 0x800) { b[n++] = 0xC0 | (c >> 6); b[n++] = 0x80 | (c & 0x3F); }
    else if (c < 0x10000) { b[n++] = 0xE0 | (c >> 12); b[n++] = 0x80 | ((c >> 6) & 0x3F); b[n++] = 0x80 | (c & 0x3F); }
    else { b[n++] = 0xF0 | (c >> 18); b[n++] = 0x80 | ((c >> 12) & 0x3F); b[n++] = 0x80 | ((c >> 6) & 0x3F); b[n++] = 0x80 | (c & 0x3F); }
    return n;
}

static lv_color_t col_of(term_view_t *tv, uint32_t c, bool fg)
{
    if (c & VT_COLOR_DEFAULT) return fg ? tv->fg : tv->bg;
    return lv_color_hex(c);
}

/* Terminal artwork fills cells, including the extra text leading. */
static bool draw_cell_art(lv_layer_t *layer, uint32_t cp, const lv_area_t *cell, lv_color_t color)
{
    bool line = cp >= 0x2500 && cp <= 0x2503;
    if (!line && (cp < 0x2580 || cp > 0x259F)) return false;
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = color; d.bg_opa = LV_OPA_COVER; d.border_width = 0; d.radius = 0;
    lv_area_t a = *cell;
    int w = lv_area_get_width(cell), h = lv_area_get_height(cell);
    if (line) {
        int thickness = (cp & 1) ? 2 : 1;
        if (cp <= 0x2501) { a.y1 += h / 2; a.y2 = a.y1 + thickness - 1; }
        else { a.x1 += w / 2; a.x2 = a.x1 + thickness - 1; }
        lv_draw_rect(layer, &d, &a); return true;
    }
    if (cp == 0x2580) a.y2 = a.y1 + (h + 1) / 2 - 1;
    else if (cp <= 0x2588) a.y1 = a.y2 + 1 - (h * (cp - 0x2580) + 7) / 8;
    else if (cp <= 0x258F) a.x2 = a.x1 + (w * (0x2590 - cp) + 7) / 8 - 1;
    else if (cp == 0x2590) a.x1 += w / 2;
    else if (cp <= 0x2593) {
        /* Density shades need one draw operation, not hundreds of dot tasks. */
        d.bg_opa = (lv_opa_t)((cp - 0x2590) * 64);
    } else if (cp == 0x2594) a.y2 = a.y1 + (h + 7) / 8 - 1;
    else if (cp == 0x2595) a.x1 = a.x2 + 1 - (w + 7) / 8;
    else {
        /* Quadrants: upper-left, upper-right, lower-left, lower-right. */
        static const uint8_t masks[] = {4,8,1,13,9,7,11,2,6,14};
        unsigned mask = masks[cp-0x2596];
        for (int q=0;q<4;q++) if (mask & (1u<<q)) {
            lv_area_t quad = {cell->x1 + (q&1 ? w/2 : 0), cell->y1 + (q&2 ? h/2 : 0),
                q&1 ? cell->x2 : cell->x1+w/2-1, q&2 ? cell->y2 : cell->y1+h/2-1};
            lv_draw_rect(layer,&d,&quad);
        }
        return true;
    }
    lv_draw_rect(layer,&d,&a);
    return true;
}

static void draw_cb(lv_event_t *e)
{
    term_view_t *tv = lv_event_get_user_data(e);
    if (!tv->vt) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t obj_area; lv_obj_get_coords(tv->obj, &obj_area);
    const lv_area_t *clip = &layer->_clip_area;
    int rows = vt_rows(tv->vt), cols = vt_cols(tv->vt);
    int cx, cy; bool cvis; vt_cursor(tv->vt, &cx, &cy, &cvis);
    char *txt = malloc(cols * 4 + 8);
    lv_draw_rect_dsc_t rd; lv_draw_rect_dsc_init(&rd); rd.radius = 0; rd.border_width = 0;
    lv_draw_label_dsc_t ld; lv_draw_label_dsc_init(&ld); ld.font = tv->font; ld.align = LV_TEXT_ALIGN_LEFT; ld.text_local = 1;
    ld.letter_space = 0; ld.line_space = 0;
    for (int y = 0; y < rows; y++) {
        int32_t ry = obj_area.y1 + y * tv->cell_h;
        if (ry + tv->cell_h < clip->y1 || ry > clip->y2) continue;
        const vt_cell_t *row = vt_view_row(tv->vt, y);
        int x = 0;
        while (x < cols) {
            /* run of cells with identical attributes */
            const vt_cell_t *c0 = &row[x];
            int x1 = x + 1;
            while (x1 < cols) {
                const vt_cell_t *c = &row[x1];
                if (c->fg != c0->fg || c->bg != c0->bg || (c->attr & ~(VT_ATTR_WIDE | VT_ATTR_WIDE_CONT)) != (c0->attr & ~(VT_ATTR_WIDE | VT_ATTR_WIDE_CONT))) break;
                x1++;
            }
            bool inv = c0->attr & VT_ATTR_INVERSE;
            lv_color_t fg = col_of(tv, c0->fg, true), bg = col_of(tv, c0->bg, false);
            if (inv) { lv_color_t t = fg; fg = bg; bg = t; }
            if (c0->attr & VT_ATTR_DIM) fg = lv_color_mix(fg, bg, 160);
            /* Bold keeps the selected foreground, as on a PC terminal. */
            lv_area_t a = { obj_area.x1 + x * tv->cell_w, ry, obj_area.x1 + x1 * tv->cell_w - 1, ry + tv->cell_h - 1 };
            bool bg_default = (c0->bg & VT_COLOR_DEFAULT) && !inv;
            if (!bg_default) { rd.bg_color = bg; rd.bg_opa = LV_OPA_COVER; lv_draw_rect(layer, &rd, &a); }
            /* Anchor every non-ASCII glyph to its VT cell, not to the font's
             * advance. Ambiguous-width pixel symbols often have a 24px glyph
             * even though PC terminals allocate them one 12px column. Keep
             * ASCII runs batched for the common fast path. */
            ld.color = fg;
            ld.decor = (c0->attr & VT_ATTR_UNDERLINE ? LV_TEXT_DECOR_UNDERLINE : 0) |
                       (c0->attr & VT_ATTR_STRIKE ? LV_TEXT_DECOR_STRIKETHROUGH : 0);
            for (int i=x; i<x1;) {
                if (row[i].attr & VT_ATTR_WIDE_CONT) { i++; continue; }
                uint32_t cp = row[i].cp ? row[i].cp : ' ';
                int width = row[i].attr & VT_ATTR_WIDE ? 2 : 1;
                lv_area_t ta = {obj_area.x1+i*tv->cell_w, ry,
                                obj_area.x1+(i+width)*tv->cell_w-1, ry+tv->cell_h-1};
                /* Orange EL: recolor Claude's salmon mascot blocks only;
                 * keep application error reds and other true colors intact. */
                lv_color_t art_fg = fg;
                if (g_settings.theme_preset == 0 && !inv &&
                    (c0->fg == 0xD77A56 || c0->fg == 0xD77757) && cp >= 0x2580 && cp <= 0x259F)
                    art_fg = lv_color_hex(g_settings.term_fg);
                if (draw_cell_art(layer,cp,&ta,art_fg)) { i+=width; continue; }
                int n=0;
                if (cp >= 0x20 && cp < 0x7f) {
                    do { txt[n++]=(char)(row[i].cp ? row[i].cp : ' '); i++; }
                    while (i<x1 && row[i].cp>=0x20 && row[i].cp<0x7f && !(row[i].attr & VT_ATTR_WIDE_CONT));
                    ta.x2=obj_area.x1+i*tv->cell_w-1;
                } else { n=utf8_put(cp,txt); i+=width; }
                txt[n]=0; ld.text=txt;
                /* No line wrapping: glyph overhang must not create another row. */
                ld.flag=LV_TEXT_FLAG_EXPAND;
                lv_area_t saved_clip = layer->_clip_area;
                lv_area_t cell_clip = {LV_MAX(saved_clip.x1,ta.x1), LV_MAX(saved_clip.y1,ta.y1),
                                       LV_MIN(saved_clip.x2,ta.x2), LV_MIN(saved_clip.y2,ta.y2)};
                if (cell_clip.x1 <= cell_clip.x2 && cell_clip.y1 <= cell_clip.y2) {
                    layer->_clip_area = cell_clip;
                    lv_draw_label(layer,&ld,&ta);
                    layer->_clip_area = saved_clip;
                }
            }
            x = x1;
        }
    }
    /* cursor */
    if (cvis && (tv->blink_on || !tv->cursor_blink) && cy >= 0 && cy < rows) {
        const vt_cell_t *row = vt_view_row(tv->vt, cy);
        int w = (cx < cols && (row[cx].attr & VT_ATTR_WIDE)) ? 2 : 1;
        lv_area_t a = { obj_area.x1 + cx * tv->cell_w, obj_area.y1 + cy * tv->cell_h,
                        obj_area.x1 + (cx + w) * tv->cell_w - 1, obj_area.y1 + (cy + 1) * tv->cell_h - 1 };
        if (tv->cursor_style == TERM_CURSOR_UNDERLINE) a.y1 = a.y2 - 2;
        else if (tv->cursor_style == TERM_CURSOR_BAR) a.x2 = a.x1 + 1;
        rd.bg_color = tv->cursor_col_set ? tv->cursor_col : tv->fg; rd.bg_opa = LV_OPA_COVER;
        lv_draw_rect(layer, &rd, &a);
        /* Only a block cursor covers the glyph, so only it has to redraw it. */
        if (cx < cols && tv->cursor_style == TERM_CURSOR_BLOCK) {
            uint32_t cp = row[cx].cp ? row[cx].cp : ' ';
            int n = utf8_put(cp, txt); txt[n] = 0;
            ld.color = tv->bg; ld.decor = 0; ld.text = txt;
            if (!draw_cell_art(layer, cp, &a, tv->bg)) lv_draw_label(layer, &ld, &a);
        }
    }
    free(txt);
}

static void blink_cb(lv_timer_t *t)
{
    term_view_t *tv = lv_timer_get_user_data(t);
    tv->blink_on = !tv->blink_on;
    if (!tv->vt) return;
    int cx, cy; bool vis; vt_cursor(tv->vt, &cx, &cy, &vis);
    if (!vis) { tv->blink_on = true; return; }
    lv_area_t oa; lv_obj_get_coords(tv->obj, &oa);
    lv_area_t a = { oa.x1 + cx * tv->cell_w, oa.y1 + cy * tv->cell_h, oa.x1 + (cx + 2) * tv->cell_w, oa.y1 + (cy + 1) * tv->cell_h };
    lv_obj_invalidate_area(tv->obj, &a);
}

static void touch_cb(lv_event_t *e)
{
    term_view_t *tv = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev || !tv->vt) return;
    lv_point_t p; lv_indev_get_point(indev, &p);
    if (code == LV_EVENT_PRESSED) { tv->press = p; tv->press_view = vt_view_offset(tv->vt); tv->dragging = false; }
    else if (code == LV_EVENT_PRESSING) {
        int dx = p.x - tv->press.x;
        int dy = p.y - tv->press.y;
        /* Mark the gesture as a drag as soon as it exceeds normal touch jitter.
         * Waiting for a whole text row allowed LVGL's long-press timer to fire
         * during a slow scroll. */
        if (abs(dx) > TERM_DRAG_SLOP || abs(dy) > TERM_DRAG_SLOP) {
            tv->dragging = true;
            int lines = dy / tv->cell_h;
            int target = tv->press_view + lines;
            int cur = vt_view_offset(tv->vt);
            if (target != cur) { vt_scroll_view(tv->vt, target - cur); lv_obj_invalidate(tv->obj); }
        }
    }
}

static void delete_cb(lv_event_t *e)
{
    term_view_t *tv = lv_event_get_user_data(e);
    if (tv->blink_timer) { lv_timer_delete(tv->blink_timer); tv->blink_timer = NULL; }
    tv->obj = NULL;
    free(tv);
}

term_view_t *term_view_create(lv_obj_t *parent, vt_t *vt, const lv_font_t *font)
{
    term_view_t *tv = calloc(1, sizeof(*tv));
    tv->vt = vt; tv->font = font;
    tv->fg = lv_color_hex(0xE5E7EB); tv->bg = lv_color_hex(0x000000);
    tv->cell_w = lv_font_get_glyph_width(font, 'M', 0);
    if (tv->cell_w <= 0) tv->cell_w = 11;
    tv->cell_h = lv_font_get_line_height(font);
    tv->blink_on = true;
    tv->cursor_blink = true;
    tv->obj = lv_obj_create(parent);
    lv_obj_set_style_bg_color(tv->obj, tv->bg, 0);
    lv_obj_set_style_bg_opa(tv->obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tv->obj, 0, 0);
    lv_obj_set_style_radius(tv->obj, 0, 0);
    lv_obj_set_style_pad_all(tv->obj, 0, 0);
    lv_obj_remove_flag(tv->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tv->obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tv->obj, draw_cb, LV_EVENT_DRAW_MAIN, tv);
    lv_obj_add_event_cb(tv->obj, touch_cb, LV_EVENT_PRESSED, tv);
    lv_obj_add_event_cb(tv->obj, touch_cb, LV_EVENT_PRESSING, tv);
    lv_obj_add_event_cb(tv->obj, delete_cb, LV_EVENT_DELETE, tv);
    tv->blink_timer = lv_timer_create(blink_cb, 550, tv);
    return tv;
}

void term_view_destroy(term_view_t *tv) { if (tv && tv->obj) lv_obj_delete(tv->obj); }
lv_obj_t *term_view_obj(term_view_t *tv) { return tv->obj; }
void term_view_set_vt(term_view_t *tv, vt_t *vt) { tv->vt = vt; lv_obj_invalidate(tv->obj); }

bool term_view_fit(term_view_t *tv, int *cols, int *rows)
{
    lv_obj_update_layout(tv->obj);
    /* Font settings can change while the view remains alive. Keep drawing,
     * scroll distances and the SSH row count on the same current metrics. */
    tv->cell_h = lv_font_get_line_height(tv->font);
    int w = lv_obj_get_width(tv->obj), h = lv_obj_get_height(tv->obj);
    int c = w / tv->cell_w, r = h / tv->cell_h;
    if (c < 10) c = 10; if (r < 3) r = 3;
    bool changed = (!tv->vt) || c != vt_cols(tv->vt) || r != vt_rows(tv->vt);
    if (cols) *cols = c; if (rows) *rows = r;
    return changed;
}

void term_view_refresh(term_view_t *tv)
{
    if (!tv->vt || !tv->obj) return;
    lv_area_t oa; lv_obj_get_coords(tv->obj, &oa);
    int rows = vt_rows(tv->vt);
    int any = 0;
    for (int y = 0; y < rows; y++) {
        if (!vt_row_dirty(tv->vt, y)) continue;
        any++;
        lv_area_t a = { oa.x1, oa.y1 + y * tv->cell_h, oa.x2, oa.y1 + (y + 1) * tv->cell_h - 1 };
        lv_obj_invalidate_area(tv->obj, &a);
    }
    if (any) { vt_clear_dirty(tv->vt); tv->blink_on = true; if (tv->blink_timer) lv_timer_reset(tv->blink_timer); }
}

void term_view_set_cursor(term_view_t *tv, term_cursor_t style, bool blink, lv_color_t color)
{
    tv->cursor_style = style;
    tv->cursor_blink = blink;
    tv->cursor_col = color;
    tv->cursor_col_set = true;
    if (!blink) tv->blink_on = true;
    lv_obj_invalidate(tv->obj);
}

void term_view_set_colors(term_view_t *tv, lv_color_t fg, lv_color_t bg)
{
    tv->fg = fg; tv->bg = bg;
    lv_obj_set_style_bg_color(tv->obj, bg, 0);
    lv_obj_invalidate(tv->obj);
}
int term_view_cell_w(term_view_t *tv) { return tv->cell_w; }
int term_view_cell_h(term_view_t *tv) { return tv->cell_h; }

bool term_view_long_press_allowed(term_view_t *tv)
{
    if (!tv || tv->dragging) return false;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return false;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    return abs(p.x - tv->press.x) <= TERM_DRAG_SLOP &&
           abs(p.y - tv->press.y) <= TERM_DRAG_SLOP;
}
