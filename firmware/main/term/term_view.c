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
    bool long_pressed; int wheel_rows;   /* gesture state while the host tracks the mouse */
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

/* Narrow the layer's clip to one cell for shapes drawn past its edges;
 * false when the cell is not visible at all. */
static bool clip_to_cell(lv_layer_t *layer, const lv_area_t *cell, lv_area_t *saved_clip)
{
    *saved_clip = layer->_clip_area;
    lv_area_t cell_clip = {LV_MAX(saved_clip->x1, cell->x1), LV_MAX(saved_clip->y1, cell->y1),
                           LV_MIN(saved_clip->x2, cell->x2), LV_MIN(saved_clip->y2, cell->y2)};
    if (cell_clip.x1 > cell_clip.x2 || cell_clip.y1 > cell_clip.y2) return false;
    layer->_clip_area = cell_clip;
    return true;
}

/* Powerline separators U+E0B0-E0BF: prompt segments must meet edge to edge,
 * so draw them over the whole cell rather than the 24 px glyph box. */
static bool draw_powerline(lv_layer_t *layer, uint32_t cp, const lv_area_t *cell, lv_color_t color)
{
    if (cp < 0xE0B0 || cp > 0xE0BF) return false;
    int32_t l = cell->x1, r = cell->x2 + 1, t = cell->y1, b = cell->y2 + 1, m = (t + b) / 2;
    int w = lv_area_get_width(cell);
    lv_area_t saved_clip;
    if (!clip_to_cell(layer, cell, &saved_clip)) return true;
    /* Corners of the solid shapes: arrows, then the four diagonal halves. */
    static const struct { uint16_t cp; uint8_t pts[6]; } solid[] = {
        {0xE0B0, {0,0, 0,2, 2,1}}, {0xE0B2, {2,0, 2,2, 0,1}},
        {0xE0B8, {0,0, 0,2, 2,2}}, {0xE0BA, {2,0, 2,2, 0,2}},
        {0xE0BC, {0,0, 2,0, 0,2}}, {0xE0BE, {0,0, 2,0, 2,2}},
    };
    const int32_t xs[] = {l, 0, r}, ys[] = {t, m, b};
    for (size_t i = 0; i < sizeof(solid) / sizeof(solid[0]); i++) {
        if (solid[i].cp != cp) continue;
        lv_draw_triangle_dsc_t d; lv_draw_triangle_dsc_init(&d);
        d.color = color; d.opa = LV_OPA_COVER;
        for (int k = 0; k < 3; k++) {
            d.p[k].x = xs[solid[i].pts[2 * k]];
            d.p[k].y = ys[solid[i].pts[2 * k + 1]];
        }
        lv_draw_triangle(layer, &d);
        layer->_clip_area = saved_clip;
        return true;
    }
    if (cp >= 0xE0B4 && cp <= 0xE0B7) {
        /* Half of a stadium twice the cell's width, clipped to the cell. */
        lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
        d.radius = LV_RADIUS_CIRCLE;
        bool thin = cp & 1;
        d.bg_color = color; d.bg_opa = thin ? LV_OPA_TRANSP : LV_OPA_COVER;
        d.border_color = color; d.border_width = thin ? 1 : 0; d.border_opa = LV_OPA_COVER;
        lv_area_t a = *cell;
        if (cp <= 0xE0B5) a.x1 -= w; else a.x2 += w;
        lv_draw_rect(layer, &d, &a);
        layer->_clip_area = saved_clip;
        return true;
    }
    /* Thin chevrons and slashes. */
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.color = color; d.opa = LV_OPA_COVER; d.width = 1;
    lv_point_precise_t p[3]; int n = 2;
    if (cp == 0xE0B1) { p[0] = (lv_point_precise_t){l, t}; p[1] = (lv_point_precise_t){r - 1, m}; p[2] = (lv_point_precise_t){l, b}; n = 3; }
    else if (cp == 0xE0B3) { p[0] = (lv_point_precise_t){r - 1, t}; p[1] = (lv_point_precise_t){l, m}; p[2] = (lv_point_precise_t){r - 1, b}; n = 3; }
    else if (cp == 0xE0B9 || cp == 0xE0BF) { p[0] = (lv_point_precise_t){l, t}; p[1] = (lv_point_precise_t){r, b}; }
    else { p[0] = (lv_point_precise_t){l, b}; p[1] = (lv_point_precise_t){r, t}; }   /* E0BB, E0BD */
    for (int k = 0; k + 1 < n; k++) { d.p1 = p[k]; d.p2 = p[k + 1]; lv_draw_line(layer, &d); }
    layer->_clip_area = saved_clip;
    return true;
}

/* Box drawing U+2500-257F, generated from the Unicode character names: two
 * bits per arm, up | right << 2 | down << 4 | left << 6, each 0 none, 1 light,
 * 2 heavy or 3 double. Dashes, arcs and diagonals are 0 and drawn apart. */
static const uint8_t s_box_arms[128] = {
    0x44,0x88,0x11,0x22,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x14,0x18,0x24,0x28,
    0x50,0x90,0x60,0xA0,0x05,0x09,0x06,0x0A,0x41,0x81,0x42,0x82,0x15,0x19,0x16,0x25,
    0x26,0x1A,0x29,0x2A,0x51,0x91,0x52,0x61,0x62,0x92,0xA1,0xA2,0x54,0x94,0x58,0x98,
    0x64,0xA4,0x68,0xA8,0x45,0x85,0x49,0x89,0x46,0x86,0x4A,0x8A,0x55,0x95,0x59,0x99,
    0x56,0x65,0x66,0x96,0x5A,0xA5,0x69,0x9A,0xA9,0xA6,0x6A,0xAA,0x00,0x00,0x00,0x00,
    0xCC,0x33,0x1C,0x34,0x3C,0xD0,0x70,0xF0,0x0D,0x07,0x0F,0xC1,0x43,0xC3,0x1D,0x37,
    0x3F,0xD1,0x73,0xF3,0xDC,0x74,0xFC,0xCD,0x47,0xCF,0xDD,0x77,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x40,0x01,0x04,0x10,0x80,0x02,0x08,0x20,0x48,0x21,0x84,0x12,
};
enum { BOX_UP, BOX_RIGHT, BOX_DOWN, BOX_LEFT };

/* A light or heavy line through centre c covers [c, c + weight - 1]; a
 * double line is two light strokes at c - 1 and c + 1. */
static int32_t box_lo(int weight, int32_t c) { return weight == 3 ? c - 1 : c; }
static int32_t box_hi(int weight, int32_t c) { return weight == 3 ? c + 1 : c + weight - 1; }

static void box_fill(lv_layer_t *layer, const lv_draw_rect_dsc_t *d, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    lv_area_t a = {LV_MIN(x1, x2), LV_MIN(y1, y2), LV_MAX(x1, x2), LV_MAX(y1, y2)};
    lv_draw_rect(layer, d, &a);
}

/* One arm from the cell edge `far` to the centre. `c` is the centre along the
 * arm, `c_cross` across it; `opposite` is the weight of the arm continuing it
 * and cross[0] / cross[1] those of the perpendicular arms on the low / high
 * side (up / down for a horizontal arm, left / right for a vertical one). */
static void box_arm(lv_layer_t *layer, const lv_draw_rect_dsc_t *d, bool horizontal, bool toward_low,
                    int weight, int opposite, const int *cross, int32_t c, int32_t c_cross, int32_t far)
{
    int32_t cross_lo = INT32_MAX, cross_hi = INT32_MIN;
    for (int k = 0; k < 2; k++) if (cross[k]) {
        cross_lo = LV_MIN(cross_lo, box_lo(cross[k], c));
        cross_hi = LV_MAX(cross_hi, box_hi(cross[k], c));
    }
    if (weight != 3) {
        /* Through lines meet at the centre; a tee stops at the nearest
         * perpendicular stroke, a corner reaches the farthest one. */
        int32_t end = c;
        if (!opposite && cross[0] && cross[1]) end = toward_low ? cross_lo : cross_hi;
        else if (!opposite && (cross[0] || cross[1])) end = toward_low ? cross_hi : cross_lo;
        if (horizontal) box_fill(layer, d, far, c_cross, end, c_cross + weight - 1);
        else box_fill(layer, d, c_cross, far, c_cross + weight - 1, end);
        return;
    }
    for (int s = 0; s < 2; s++) {
        /* Each stroke of a double arm stops short of a perpendicular arm on
         * its own side and runs on to the far edge of one on the other side. */
        int same = cross[s], other = cross[1 - s];
        int32_t end = c;
        if (same) end = toward_low ? box_lo(same, c) : box_hi(same, c);
        else if (other) end = toward_low ? box_hi(other, c) : box_lo(other, c);
        int32_t at = c_cross + (s ? 1 : -1);
        if (horizontal) box_fill(layer, d, far, at, end, at);
        else box_fill(layer, d, at, far, at, end);
    }
}

/* Box drawing fills the cell, so frames stay joined across the row spacing. */
static bool draw_box(lv_layer_t *layer, uint32_t cp, const lv_area_t *cell, lv_color_t color)
{
    if (cp < 0x2500 || cp > 0x257F) return false;
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = color; d.bg_opa = LV_OPA_COVER; d.border_width = 0; d.radius = 0;
    int32_t x1 = cell->x1, y1 = cell->y1, x2 = cell->x2, y2 = cell->y2;
    int w = lv_area_get_width(cell), h = lv_area_get_height(cell);
    int32_t cx = x1 + w / 2, cy = y1 + h / 2;

    int dashes = 0, idx = 0;
    if (cp >= 0x2504 && cp <= 0x250B) { idx = cp - 0x2504; dashes = idx < 4 ? 3 : 4; }
    else if (cp >= 0x254C && cp <= 0x254F) { idx = cp - 0x254C; dashes = 2; }
    if (dashes) {
        /* Even index light, odd heavy; the second pair of each group is vertical. */
        int weight = (idx & 1) ? 2 : 1;
        bool vertical = idx & 2;
        int len = vertical ? h : w, gap = LV_MAX(1, len / (2 * dashes));
        for (int k = 0; k < dashes; k++) {
            int32_t a = k * len / dashes + gap / 2, b = (k + 1) * len / dashes - (gap + 1) / 2 - 1;
            if (vertical) box_fill(layer, &d, cx, y1 + a, cx + weight - 1, y1 + b);
            else box_fill(layer, &d, x1 + a, cy, x1 + b, cy + weight - 1);
        }
        return true;
    }

    lv_area_t saved_clip;
    if (cp >= 0x256D && cp <= 0x2573) {
        if (!clip_to_cell(layer, cell, &saved_clip)) return true;
        if (cp <= 0x2570) {
            /* Rounded corner: a bordered rectangle whose rounded corner sits on
             * the cell centre and whose other sides lie outside the clip. */
            int32_t r = LV_MIN(w, h) / 2, out = r + 2;
            lv_draw_rect_dsc_t b; lv_draw_rect_dsc_init(&b);
            b.bg_opa = LV_OPA_TRANSP; b.radius = r;
            b.border_color = color; b.border_width = 1; b.border_opa = LV_OPA_COVER;
            bool right = cp == 0x256D || cp == 0x2570, down = cp <= 0x256E;
            lv_area_t a = {right ? cx : x1 - out, down ? cy : y1 - out,
                           right ? x2 + out : cx, down ? y2 + out : cy};
            lv_draw_rect(layer, &b, &a);
        } else {
            lv_draw_line_dsc_t l; lv_draw_line_dsc_init(&l);
            l.color = color; l.opa = LV_OPA_COVER; l.width = 1;
            if (cp != 0x2572) {   /* upper right to lower left */
                l.p1 = (lv_point_precise_t){x2 + 1, y1}; l.p2 = (lv_point_precise_t){x1, y2 + 1};
                lv_draw_line(layer, &l);
            }
            if (cp != 0x2571) {   /* upper left to lower right */
                l.p1 = (lv_point_precise_t){x1, y1}; l.p2 = (lv_point_precise_t){x2 + 1, y2 + 1};
                lv_draw_line(layer, &l);
            }
        }
        layer->_clip_area = saved_clip;
        return true;
    }

    uint8_t v = s_box_arms[cp - 0x2500];
    int arm[4] = {v & 3, (v >> 2) & 3, (v >> 4) & 3, v >> 6};
    const int vert[2] = {arm[BOX_UP], arm[BOX_DOWN]}, horiz[2] = {arm[BOX_LEFT], arm[BOX_RIGHT]};
    if (arm[BOX_LEFT]) box_arm(layer, &d, true, true, arm[BOX_LEFT], arm[BOX_RIGHT], vert, cx, cy, x1);
    if (arm[BOX_RIGHT]) box_arm(layer, &d, true, false, arm[BOX_RIGHT], arm[BOX_LEFT], vert, cx, cy, x2);
    if (arm[BOX_UP]) box_arm(layer, &d, false, true, arm[BOX_UP], arm[BOX_DOWN], horiz, cy, cx, y1);
    if (arm[BOX_DOWN]) box_arm(layer, &d, false, false, arm[BOX_DOWN], arm[BOX_UP], horiz, cy, cx, y2);
    return true;
}

/* Solid triangles used as prompt characters (▶ ▸ ► ...). They are one
 * column wide in a terminal, but the CJK faces draw them a full 24 px wide,
 * so the cell clip would keep only half; draw them to fit the cell instead. */
static bool draw_triangle_symbol(lv_layer_t *layer, uint32_t cp, const lv_area_t *cell, lv_color_t color)
{
    /* Direction: 0 up, 1 right, 2 down, 3 left. Size: 0 large, 1 small, 2 pointer. */
    static const struct { uint16_t cp; uint8_t dir, size; } shapes[] = {
        {0x25B2, 0, 0}, {0x25B4, 0, 1}, {0x25B6, 1, 0}, {0x25B8, 1, 1}, {0x25BA, 1, 2},
        {0x25BC, 2, 0}, {0x25BE, 2, 1}, {0x25C0, 3, 0}, {0x25C2, 3, 1}, {0x25C4, 3, 2},
    };
    size_t i = 0;
    while (i < sizeof(shapes) / sizeof(shapes[0]) && shapes[i].cp != cp) i++;
    if (i == sizeof(shapes) / sizeof(shapes[0])) return false;
    int w = lv_area_get_width(cell), h = lv_area_get_height(cell);
    int32_t cx = cell->x1 + w / 2, cy = cell->y1 + h / 2;
    /* Half extents along and across the pointing direction. */
    int32_t along = shapes[i].size == 1 ? w / 4 : (w - 2) / 2;
    int32_t across = shapes[i].size == 0 ? (w - 2) * 3 / 5 : shapes[i].size == 1 ? w / 4 + 1 : w / 3;
    int dir = shapes[i].dir;
    int32_t sx = dir == 1 ? 1 : dir == 3 ? -1 : 0, sy = dir == 2 ? 1 : dir == 0 ? -1 : 0;
    lv_draw_triangle_dsc_t d; lv_draw_triangle_dsc_init(&d);
    d.color = color; d.opa = LV_OPA_COVER;
    d.p[0].x = cx + sx * along; d.p[0].y = cy + sy * along;          /* tip */
    d.p[1].x = cx - sx * along - sy * across; d.p[1].y = cy - sy * along + sx * across;
    d.p[2].x = cx - sx * along + sy * across; d.p[2].y = cy - sy * along - sx * across;
    lv_draw_triangle(layer, &d);
    return true;
}

/* Terminal artwork fills cells, including the extra text leading. */
static bool draw_cell_art(lv_layer_t *layer, uint32_t cp, const lv_area_t *cell, lv_color_t color)
{
    if (draw_powerline(layer, cp, cell, color) || draw_box(layer, cp, cell, color) ||
        draw_triangle_symbol(layer, cp, cell, color)) return true;
    if (cp < 0x2580 || cp > 0x259F) return false;
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = color; d.bg_opa = LV_OPA_COVER; d.border_width = 0; d.radius = 0;
    lv_area_t a = *cell;
    int w = lv_area_get_width(cell), h = lv_area_get_height(cell);
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
                int n=0; int32_t label_dx=0;
                if (cp >= 0x20 && cp < 0x7f) {
                    do { txt[n++]=(char)(row[i].cp ? row[i].cp : ' '); i++; }
                    while (i<x1 && row[i].cp>=0x20 && row[i].cp<0x7f && !(row[i].attr & VT_ATTR_WIDE_CONT));
                    ta.x2=obj_area.x1+i*tv->cell_w-1;
                } else {
                    n=utf8_put(cp,txt); i+=width;
                    /* A glyph still wider than its cells loses both edges
                     * evenly rather than its whole right half. */
                    lv_font_glyph_dsc_t g;
                    int32_t span=lv_area_get_width(&ta);
                    if (lv_font_get_glyph_dsc(tv->font,&g,cp,0) && g.adv_w>span) label_dx=(g.adv_w-span)/2;
                }
                txt[n]=0; ld.text=txt;
                /* No line wrapping: glyph overhang must not create another row. */
                ld.flag=LV_TEXT_FLAG_EXPAND;
                lv_area_t saved_clip = layer->_clip_area;
                lv_area_t cell_clip = {LV_MAX(saved_clip.x1,ta.x1), LV_MAX(saved_clip.y1,ta.y1),
                                       LV_MIN(saved_clip.x2,ta.x2), LV_MIN(saved_clip.y2,ta.y2)};
                if (cell_clip.x1 <= cell_clip.x2 && cell_clip.y1 <= cell_clip.y2) {
                    layer->_clip_area = cell_clip;
                    lv_area_t la=ta; la.x1-=label_dx;
                    lv_draw_label(layer,&ld,&la);
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

/* With mouse tracking on (herdr, vim, htop ...) a tap is a left click, a long
 * press a right click (herdr's close / split menus) and a vertical swipe turns
 * the wheel one notch per row. Otherwise a swipe scrolls the local scrollback
 * and a long press opens the terminal menu. */
static void touch_cb(lv_event_t *e)
{
    term_view_t *tv = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev || !tv->vt) return;
    lv_point_t p; lv_indev_get_point(indev, &p);
    bool mouse = vt_mouse_enabled(tv->vt) && vt_view_offset(tv->vt) == 0;
    lv_area_t oa; lv_obj_get_coords(tv->obj, &oa);
    int col = (tv->press.x - oa.x1) / tv->cell_w, row = (tv->press.y - oa.y1) / tv->cell_h;
    if (code == LV_EVENT_PRESSED) {
        tv->press = p; tv->press_view = vt_view_offset(tv->vt); tv->dragging = false;
        tv->long_pressed = false; tv->wheel_rows = 0;
    } else if (code == LV_EVENT_PRESSING) {
        int dx = p.x - tv->press.x;
        int dy = p.y - tv->press.y;
        /* Mark the gesture as a drag as soon as it exceeds normal touch jitter.
         * Waiting for a whole text row allowed LVGL's long-press timer to fire
         * during a slow scroll. */
        if (abs(dx) > TERM_DRAG_SLOP || abs(dy) > TERM_DRAG_SLOP) {
            tv->dragging = true;
            int lines = dy / tv->cell_h;
            if (mouse) {
                /* Finger down shows earlier lines, as the scrollback does: wheel up. */
                for (; tv->wheel_rows < lines; tv->wheel_rows++) vt_mouse_report(tv->vt, VT_MOUSE_PRESS, 64, col, row);
                for (; tv->wheel_rows > lines; tv->wheel_rows--) vt_mouse_report(tv->vt, VT_MOUSE_PRESS, 65, col, row);
                return;
            }
            int target = tv->press_view + lines;
            int cur = vt_view_offset(tv->vt);
            if (target != cur) { vt_scroll_view(tv->vt, target - cur); lv_obj_invalidate(tv->obj); }
        }
    } else if (code == LV_EVENT_LONG_PRESSED) {
        tv->long_pressed = true;
        if (mouse && !tv->dragging) {
            vt_mouse_report(tv->vt, VT_MOUSE_PRESS, 2, col, row);
            vt_mouse_report(tv->vt, VT_MOUSE_RELEASE, 2, col, row);
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (mouse && !tv->dragging && !tv->long_pressed) {
            vt_mouse_report(tv->vt, VT_MOUSE_PRESS, 0, col, row);
            vt_mouse_report(tv->vt, VT_MOUSE_RELEASE, 0, col, row);
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
    lv_obj_add_event_cb(tv->obj, touch_cb, LV_EVENT_LONG_PRESSED, tv);
    lv_obj_add_event_cb(tv->obj, touch_cb, LV_EVENT_RELEASED, tv);
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
    /* While the host tracks the mouse the long press is its right click. */
    if (!tv || tv->dragging || (tv->vt && vt_mouse_enabled(tv->vt) && vt_view_offset(tv->vt) == 0)) return false;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return false;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    return abs(p.x - tv->press.x) <= TERM_DRAG_SLOP &&
           abs(p.y - tv->press.y) <= TERM_DRAG_SLOP;
}
