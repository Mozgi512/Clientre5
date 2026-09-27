/* Compact VT100/xterm-256color terminal emulator with UTF-8 and East Asian
 * wide character support. */
#include "vt.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_heap_caps.h"

#define MAX_PARAMS 16
#define OSC_MAX 256

enum { ST_GROUND, ST_ESC, ST_CSI, ST_OSC, ST_DCS, ST_CHARSET, ST_ESC_HASH };

struct vt {
    int cols, rows;
    vt_cell_t *main;         /* rows*cols */
    vt_cell_t *alt;
    vt_cell_t *screen;       /* points to main or alt */
    vt_cell_t *sb;           /* scrollback ring, sb_cap lines of cols cells */
    int sb_cap, sb_head, sb_used;
    int view_off;
    uint8_t *dirty;
    int cx, cy;
    int saved_cx, saved_cy; uint32_t saved_fg, saved_bg; uint8_t saved_attr;
    int scroll_top, scroll_bot;
    uint32_t fg, bg; uint8_t attr;
    bool cursor_visible, autowrap, origin_mode, insert_mode, app_cursor, app_keypad, bracketed_paste, mouse;
    bool wrap_pending;
    bool alt_active;
    int charset_g0;          /* 0 = ASCII, 1 = DEC graphics */
    int state;
    int params[MAX_PARAMS]; int nparams; bool priv;
    char osc[OSC_MAX]; int osc_len;
    uint32_t utf8_cp; int utf8_left;
    char title[64];
    vt_write_cb_t write_cb; void *write_user;
    vt_bell_cb_t bell_cb; void *bell_user;
    vt_title_cb_t title_cb; void *title_user;
    uint8_t tabs[512 / 8];
};

/* ------------------------------------------------------------ wcwidth */
static const struct { uint32_t a, b; } s_wide[] = {
    {0x1100,0x115F},{0x231A,0x231B},{0x2329,0x232A},{0x23E9,0x23EC},{0x23F0,0x23F0},{0x23F3,0x23F3},
    {0x25FD,0x25FE},{0x2614,0x2615},{0x2648,0x2653},{0x267F,0x267F},{0x2693,0x2693},{0x26A1,0x26A1},
    {0x26AA,0x26AB},{0x26BD,0x26BE},{0x26C4,0x26C5},{0x26CE,0x26CE},{0x26D4,0x26D4},{0x26EA,0x26EA},
    {0x26F2,0x26F3},{0x26F5,0x26F5},{0x26FA,0x26FA},{0x26FD,0x26FD},{0x2705,0x2705},{0x270A,0x270B},
    {0x2728,0x2728},{0x274C,0x274C},{0x274E,0x274E},{0x2753,0x2755},{0x2757,0x2757},{0x2795,0x2797},
    {0x27B0,0x27B0},{0x27BF,0x27BF},{0x2B1B,0x2B1C},{0x2B50,0x2B50},{0x2B55,0x2B55},{0x2E80,0x303E},
    {0x3041,0x33FF},{0x3400,0x4DBF},{0x4E00,0x9FFF},{0xA000,0xA4CF},{0xA960,0xA97F},{0xAC00,0xD7A3},
    {0xF900,0xFAFF},{0xFE10,0xFE19},{0xFE30,0xFE6F},{0xFF00,0xFF60},{0xFFE0,0xFFE6},{0x1F004,0x1F004},
    {0x1F0CF,0x1F0CF},{0x1F18E,0x1F18E},{0x1F191,0x1F19A},{0x1F200,0x1F251},{0x1F300,0x1F64F},
    {0x1F680,0x1F6FF},{0x1F900,0x1F9FF},{0x20000,0x3FFFD},
};
int vt_wcwidth(uint32_t cp)
{
    if (cp == 0) return 0;
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return 0;
    if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x200B && cp <= 0x200F) || cp == 0xFEFF ||
        (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0x3099 && cp <= 0x309A)) return 0;
    for (size_t i = 0; i < sizeof(s_wide) / sizeof(s_wide[0]); i++)
        if (cp >= s_wide[i].a && cp <= s_wide[i].b) return 2;
    return 1;
}

/* ------------------------------------------------------------ helpers */
static inline vt_cell_t *cell(vt_t *v, int x, int y) { return &v->screen[y * v->cols + x]; }
static inline void mark_dirty(vt_t *v, int y) { if (y >= 0 && y < v->rows) v->dirty[y] = 1; }
static void mark_all(vt_t *v) { memset(v->dirty, 1, v->rows); }

static void blank_cell(vt_t *v, vt_cell_t *c)
{
    c->cp = ' '; c->fg = v->fg; c->bg = v->bg; c->attr = 0;
}
static void clear_cells(vt_t *v, int y, int x0, int x1)
{
    if (y < 0 || y >= v->rows) return;
    if (x0 < 0) x0 = 0; if (x1 > v->cols) x1 = v->cols;
    for (int x = x0; x < x1; x++) blank_cell(v, cell(v, x, y));
    mark_dirty(v, y);
}
static void clear_rows(vt_t *v, int y0, int y1)
{
    for (int y = y0; y < y1; y++) clear_cells(v, y, 0, v->cols);
}

static void sb_push(vt_t *v, const vt_cell_t *row)
{
    if (!v->sb || v->alt_active) return;
    memcpy(&v->sb[(size_t)v->sb_head * v->cols], row, sizeof(vt_cell_t) * v->cols);
    v->sb_head = (v->sb_head + 1) % v->sb_cap;
    if (v->sb_used < v->sb_cap) v->sb_used++;
}

static void scroll_up(vt_t *v, int top, int bot, int n)   /* bot inclusive */
{
    if (n <= 0) return;
    if (n > bot - top + 1) n = bot - top + 1;
    for (int i = 0; i < n; i++) if (top == 0) sb_push(v, cell(v, 0, top + i));
    memmove(cell(v, 0, top), cell(v, 0, top + n), sizeof(vt_cell_t) * v->cols * (bot - top + 1 - n));
    for (int y = bot - n + 1; y <= bot; y++) clear_cells(v, y, 0, v->cols);
    for (int y = top; y <= bot; y++) mark_dirty(v, y);
}
static void scroll_down(vt_t *v, int top, int bot, int n)
{
    if (n <= 0) return;
    if (n > bot - top + 1) n = bot - top + 1;
    memmove(cell(v, 0, top + n), cell(v, 0, top), sizeof(vt_cell_t) * v->cols * (bot - top + 1 - n));
    for (int y = top; y < top + n; y++) clear_cells(v, y, 0, v->cols);
    for (int y = top; y <= bot; y++) mark_dirty(v, y);
}

static void respond(vt_t *v, const char *s) { if (v->write_cb) v->write_cb((const uint8_t *)s, strlen(s), v->write_user); }

static void set_cursor(vt_t *v, int x, int y)
{
    int top = v->origin_mode ? v->scroll_top : 0, bot = v->origin_mode ? v->scroll_bot : v->rows - 1;
    if (x < 0) x = 0; if (x >= v->cols) x = v->cols - 1;
    if (y < top) y = top; if (y > bot) y = bot;
    mark_dirty(v, v->cy);
    v->cx = x; v->cy = y; v->wrap_pending = false;
    mark_dirty(v, v->cy);
}

static void linefeed(vt_t *v)
{
    mark_dirty(v, v->cy);
    if (v->cy == v->scroll_bot) scroll_up(v, v->scroll_top, v->scroll_bot, 1);
    else if (v->cy < v->rows - 1) v->cy++;
    mark_dirty(v, v->cy);
    v->wrap_pending = false;
}
static void reverse_index(vt_t *v)
{
    if (v->cy == v->scroll_top) scroll_down(v, v->scroll_top, v->scroll_bot, 1);
    else if (v->cy > 0) v->cy--;
    mark_dirty(v, v->cy);
}

static uint32_t dec_graphics(uint32_t c)
{
    static const uint16_t map[] = { /* 0x60..0x7E */
        0x25C6,0x2592,0x2409,0x240C,0x240D,0x240A,0x00B0,0x00B1,0x2424,0x240B,0x2518,0x2510,0x250C,0x2514,0x253C,
        0x23BA,0x23BB,0x2500,0x23BC,0x23BD,0x251C,0x2524,0x2534,0x252C,0x2502,0x2264,0x2265,0x03C0,0x2260,0x00A3,0x00B7 };
    if (c >= 0x60 && c <= 0x7E) return map[c - 0x60];
    return c;
}

static void put_char(vt_t *v, uint32_t cp)
{
    if (v->charset_g0 == 1) cp = dec_graphics(cp);
    int w = vt_wcwidth(cp);
    if (w == 0) {
        /* combining: attach to previous cell (we just ignore for rendering simplicity) */
        return;
    }
    if (v->wrap_pending) {
        if (v->autowrap) { v->cx = 0; linefeed(v); }
        else v->cx = v->cols - 1;
        v->wrap_pending = false;
    }
    if (w == 2 && v->cx == v->cols - 1) {
        /* wide char does not fit: pad and wrap */
        blank_cell(v, cell(v, v->cx, v->cy));
        if (v->autowrap) { v->cx = 0; linefeed(v); }
    }
    if (v->insert_mode) {
        memmove(cell(v, v->cx + w, v->cy), cell(v, v->cx, v->cy), sizeof(vt_cell_t) * (v->cols - v->cx - w));
    }
    vt_cell_t *c = cell(v, v->cx, v->cy);
    /* if overwriting the 2nd half of a wide char, blank its first half */
    if (v->cx > 0 && (c->attr & VT_ATTR_WIDE_CONT)) { vt_cell_t *p = cell(v, v->cx - 1, v->cy); p->cp = ' '; p->attr &= ~VT_ATTR_WIDE; }
    /* if overwriting the first half of a wide char, blank its continuation */
    if ((c->attr & VT_ATTR_WIDE) && v->cx + 1 < v->cols) { vt_cell_t *n = cell(v, v->cx + 1, v->cy); n->cp = ' '; n->attr &= ~VT_ATTR_WIDE_CONT; }
    c->cp = cp; c->fg = v->fg; c->bg = v->bg; c->attr = v->attr & ~(VT_ATTR_WIDE | VT_ATTR_WIDE_CONT);
    if (w == 2) {
        c->attr |= VT_ATTR_WIDE;
        if (v->cx + 1 < v->cols) {
            vt_cell_t *n = cell(v, v->cx + 1, v->cy);
            if (n->attr & VT_ATTR_WIDE) { if (v->cx + 2 < v->cols) { vt_cell_t *nn = cell(v, v->cx + 2, v->cy); nn->cp = ' '; nn->attr &= ~VT_ATTR_WIDE_CONT; } }
            n->cp = 0; n->fg = v->fg; n->bg = v->bg; n->attr = (v->attr & ~VT_ATTR_WIDE) | VT_ATTR_WIDE_CONT;
        }
    }
    mark_dirty(v, v->cy);
    v->cx += w;
    if (v->cx >= v->cols) { v->cx = v->cols - 1; v->wrap_pending = true; }
}

/* ------------------------------------------------------------ SGR */
/* Mutable so the terminal theme can repaint it; already-written cells keep the
 * colour they were drawn with, new output picks up the change. */
static uint32_t s_ansi16[16] = {
    0x000000,0xCD3131,0x0DBC79,0xE5E510,0x2472C8,0xBC3FBC,0x11A8CD,0xE5E5E5,
    0x666666,0xF14C4C,0x23D18B,0xF5F543,0x3B8EEA,0xD670D6,0x29B8DB,0xFFFFFF };
static uint32_t color256(int n)
{
    if (n < 16) return s_ansi16[n];
    if (n < 232) { n -= 16; int r = n / 36, g = (n / 6) % 6, b = n % 6;
        const int lv[6] = { 0, 95, 135, 175, 215, 255 }; return (lv[r] << 16) | (lv[g] << 8) | lv[b]; }
    int g = 8 + (n - 232) * 10; return (g << 16) | (g << 8) | g;
}

static void do_sgr(vt_t *v)
{
    if (v->nparams == 0) { v->params[0] = 0; v->nparams = 1; }
    for (int i = 0; i < v->nparams; i++) {
        int p = v->params[i];
        if (p == 0) { v->fg = v->bg = VT_COLOR_DEFAULT; v->attr = 0; }
        else if (p == 1) v->attr |= VT_ATTR_BOLD;
        else if (p == 2) v->attr |= VT_ATTR_DIM;
        else if (p == 3) v->attr |= VT_ATTR_ITALIC;
        else if (p == 4) v->attr |= VT_ATTR_UNDERLINE;
        else if (p == 7) v->attr |= VT_ATTR_INVERSE;
        else if (p == 9) v->attr |= VT_ATTR_STRIKE;
        else if (p == 22) v->attr &= ~(VT_ATTR_BOLD | VT_ATTR_DIM);
        else if (p == 23) v->attr &= ~VT_ATTR_ITALIC;
        else if (p == 24) v->attr &= ~VT_ATTR_UNDERLINE;
        else if (p == 27) v->attr &= ~VT_ATTR_INVERSE;
        else if (p == 29) v->attr &= ~VT_ATTR_STRIKE;
        else if (p >= 30 && p <= 37) v->fg = s_ansi16[p - 30];
        else if (p == 39) v->fg = VT_COLOR_DEFAULT;
        else if (p >= 40 && p <= 47) v->bg = s_ansi16[p - 40];
        else if (p == 49) v->bg = VT_COLOR_DEFAULT;
        else if (p >= 90 && p <= 97) v->fg = s_ansi16[p - 90 + 8];
        else if (p >= 100 && p <= 107) v->bg = s_ansi16[p - 100 + 8];
        else if (p == 38 || p == 48) {
            uint32_t col = VT_COLOR_DEFAULT;
            if (i + 1 < v->nparams && v->params[i + 1] == 5 && i + 2 < v->nparams) { col = color256(v->params[i + 2] & 255); i += 2; }
            else if (i + 1 < v->nparams && v->params[i + 1] == 2 && i + 4 < v->nparams) {
                col = ((v->params[i + 2] & 255) << 16) | ((v->params[i + 3] & 255) << 8) | (v->params[i + 4] & 255); i += 4; }
            if (p == 38) v->fg = col; else v->bg = col;
        }
    }
}

/* ------------------------------------------------------------ modes */
static void set_mode(vt_t *v, bool on)
{
    for (int i = 0; i < v->nparams; i++) {
        int p = v->params[i];
        if (v->priv) {
            switch (p) {
            case 1: v->app_cursor = on; break;
            case 3: break;
            case 6: v->origin_mode = on; set_cursor(v, 0, v->origin_mode ? v->scroll_top : 0); break;
            case 7: v->autowrap = on; break;
            case 12: break;
            case 25: v->cursor_visible = on; mark_dirty(v, v->cy); break;
            case 1000: case 1002: case 1003: case 1006: v->mouse = on; break;
            case 2004: v->bracketed_paste = on; break;
            case 47: case 1047: case 1049:
                if (on && !v->alt_active) {
                    if (p == 1049) { v->saved_cx = v->cx; v->saved_cy = v->cy; v->saved_fg = v->fg; v->saved_bg = v->bg; v->saved_attr = v->attr; }
                    v->screen = v->alt; v->alt_active = true;
                    clear_rows(v, 0, v->rows);
                    v->view_off = 0;
                } else if (!on && v->alt_active) {
                    v->screen = v->main; v->alt_active = false;
                    if (p == 1049) { v->cx = v->saved_cx; v->cy = v->saved_cy; v->fg = v->saved_fg; v->bg = v->saved_bg; v->attr = v->saved_attr; }
                }
                mark_all(v);
                break;
            default: break;
            }
        } else {
            if (p == 4) v->insert_mode = on;
        }
    }
}

/* ------------------------------------------------------------ CSI */
#define P(i, def) ((i) < v->nparams && v->params[i] > 0 ? v->params[i] : (def))

static void do_csi(vt_t *v, char final)
{
    int n;
    switch (final) {
    case '@': n = P(0, 1); if (n > v->cols - v->cx) n = v->cols - v->cx;
        memmove(cell(v, v->cx + n, v->cy), cell(v, v->cx, v->cy), sizeof(vt_cell_t) * (v->cols - v->cx - n));
        clear_cells(v, v->cy, v->cx, v->cx + n); break;
    case 'A': set_cursor(v, v->cx, v->cy - P(0, 1)); break;
    case 'B': case 'e': set_cursor(v, v->cx, v->cy + P(0, 1)); break;
    case 'C': case 'a': set_cursor(v, v->cx + P(0, 1), v->cy); break;
    case 'D': set_cursor(v, v->cx - P(0, 1), v->cy); break;
    case 'E': set_cursor(v, 0, v->cy + P(0, 1)); break;
    case 'F': set_cursor(v, 0, v->cy - P(0, 1)); break;
    case 'G': case '`': set_cursor(v, P(0, 1) - 1, v->cy); break;
    case 'H': case 'f': { int row = P(0, 1) - 1, col = P(1, 1) - 1; if (v->origin_mode) row += v->scroll_top; set_cursor(v, col, row); break; }
    case 'I': for (n = P(0, 1); n > 0; n--) { int x = v->cx + 1; while (x < v->cols - 1 && !(v->tabs[x >> 3] & (1 << (x & 7)))) x++; v->cx = x; } mark_dirty(v, v->cy); break;
    case 'J': n = (v->nparams ? v->params[0] : 0);
        if (n == 0) { clear_cells(v, v->cy, v->cx, v->cols); clear_rows(v, v->cy + 1, v->rows); }
        else if (n == 1) { clear_rows(v, 0, v->cy); clear_cells(v, v->cy, 0, v->cx + 1); }
        else if (n == 2 || n == 3) { clear_rows(v, 0, v->rows); }
        break;
    case 'K': n = (v->nparams ? v->params[0] : 0);
        if (n == 0) clear_cells(v, v->cy, v->cx, v->cols);
        else if (n == 1) clear_cells(v, v->cy, 0, v->cx + 1);
        else clear_cells(v, v->cy, 0, v->cols);
        break;
    case 'L': if (v->cy >= v->scroll_top && v->cy <= v->scroll_bot) scroll_down(v, v->cy, v->scroll_bot, P(0, 1)); break;
    case 'M': if (v->cy >= v->scroll_top && v->cy <= v->scroll_bot) scroll_up(v, v->cy, v->scroll_bot, P(0, 1)); break;
    case 'P': n = P(0, 1); if (n > v->cols - v->cx) n = v->cols - v->cx;
        memmove(cell(v, v->cx, v->cy), cell(v, v->cx + n, v->cy), sizeof(vt_cell_t) * (v->cols - v->cx - n));
        clear_cells(v, v->cy, v->cols - n, v->cols); mark_dirty(v, v->cy); break;
    case 'S': scroll_up(v, v->scroll_top, v->scroll_bot, P(0, 1)); break;
    case 'T': scroll_down(v, v->scroll_top, v->scroll_bot, P(0, 1)); break;
    case 'X': n = P(0, 1); clear_cells(v, v->cy, v->cx, v->cx + n); break;
    case 'Z': for (n = P(0, 1); n > 0; n--) { int x = v->cx - 1; while (x > 0 && !(v->tabs[x >> 3] & (1 << (x & 7)))) x--; v->cx = x < 0 ? 0 : x; } mark_dirty(v, v->cy); break;
    case 'c': if (!v->priv) respond(v, "\x1b[?62;22c"); break;
    case 'd': set_cursor(v, v->cx, P(0, 1) - 1); break;
    case 'g': if (v->nparams && v->params[0] == 3) memset(v->tabs, 0, sizeof(v->tabs)); else v->tabs[v->cx >> 3] &= ~(1 << (v->cx & 7)); break;
    case 'h': set_mode(v, true); break;
    case 'l': set_mode(v, false); break;
    case 'm': if (!v->priv) do_sgr(v); break;
    case 'n': if (v->nparams && v->params[0] == 6) { char b[32]; snprintf(b, sizeof(b), "\x1b[%d;%dR", v->cy + 1, v->cx + 1); respond(v, b); }
              else if (v->nparams && v->params[0] == 5) respond(v, "\x1b[0n"); break;
    case 'r': { int top = P(0, 1) - 1, bot = P(1, v->rows) - 1;
        if (top < 0) top = 0; if (bot >= v->rows) bot = v->rows - 1;
        if (top < bot) { v->scroll_top = top; v->scroll_bot = bot; set_cursor(v, 0, v->origin_mode ? top : 0); } break; }
    case 's': v->saved_cx = v->cx; v->saved_cy = v->cy; break;
    case 'u': set_cursor(v, v->saved_cx, v->saved_cy); break;
    case 't': /* window ops: report size for 18 */
        if (v->nparams && v->params[0] == 18) { char b[32]; snprintf(b, sizeof(b), "\x1b[8;%d;%dt", v->rows, v->cols); respond(v, b); }
        break;
    default: break;
    }
}

static void do_osc(vt_t *v)
{
    v->osc[v->osc_len] = 0;
    int code = atoi(v->osc);
    char *p = strchr(v->osc, ';');
    if ((code == 0 || code == 2) && p) {
        strlcpy(v->title, p + 1, sizeof(v->title));
        if (v->title_cb) v->title_cb(v->title, v->title_user);
    }
}

/* ------------------------------------------------------------ input */
static void handle_ctrl(vt_t *v, uint8_t c)
{
    switch (c) {
    case 0x07: if (v->bell_cb) v->bell_cb(v->bell_user); break;
    case 0x08: if (v->cx > 0) { v->cx--; v->wrap_pending = false; mark_dirty(v, v->cy); } break;
    case 0x09: { int x = v->cx + 1; while (x < v->cols - 1 && !(v->tabs[x >> 3] & (1 << (x & 7)))) x++; v->cx = x; v->wrap_pending = false; mark_dirty(v, v->cy); break; }
    case 0x0A: case 0x0B: case 0x0C: linefeed(v); break;
    case 0x0D: v->cx = 0; v->wrap_pending = false; mark_dirty(v, v->cy); break;
    case 0x0E: v->charset_g0 = 1; break;   /* SO: G1 (we treat as graphics) */
    case 0x0F: v->charset_g0 = 0; break;
    default: break;
    }
}

static void process(vt_t *v, uint32_t c)
{
    switch (v->state) {
    case ST_GROUND:
        if (c == 0x1B) { v->state = ST_ESC; return; }
        if (c < 0x20) { handle_ctrl(v, c); return; }
        if (c == 0x7F) return;
        if (c >= 0x80 && c < 0xA0) return;
        put_char(v, c);
        return;
    case ST_ESC:
        switch (c) {
        case '[': v->state = ST_CSI; v->nparams = 0; v->priv = false; memset(v->params, 0, sizeof(v->params)); return;
        case ']': v->state = ST_OSC; v->osc_len = 0; return;
        case 'P': case '^': case '_': v->state = ST_DCS; return;
        case '(': case ')': case '*': case '+': v->state = ST_CHARSET; return;
        case '#': v->state = ST_ESC_HASH; return;
        case '7': v->saved_cx = v->cx; v->saved_cy = v->cy; v->saved_fg = v->fg; v->saved_bg = v->bg; v->saved_attr = v->attr; break;
        case '8': v->cx = v->saved_cx; v->cy = v->saved_cy; v->fg = v->saved_fg; v->bg = v->saved_bg; v->attr = v->saved_attr; mark_all(v); break;
        case 'D': linefeed(v); break;
        case 'E': v->cx = 0; linefeed(v); break;
        case 'H': v->tabs[v->cx >> 3] |= 1 << (v->cx & 7); break;
        case 'M': reverse_index(v); break;
        case 'c': vt_reset(v); break;
        case '=': v->app_keypad = true; break;
        case '>': v->app_keypad = false; break;
        case 'Z': respond(v, "\x1b[?62;22c"); break;
        default: break;
        }
        v->state = ST_GROUND;
        return;
    case ST_CHARSET:
        v->charset_g0 = (c == '0') ? 1 : 0;
        v->state = ST_GROUND;
        return;
    case ST_ESC_HASH:
        if (c == '8') { for (int y = 0; y < v->rows; y++) for (int x = 0; x < v->cols; x++) { vt_cell_t *ce = cell(v, x, y); ce->cp = 'E'; ce->fg = ce->bg = VT_COLOR_DEFAULT; ce->attr = 0; } mark_all(v); }
        v->state = ST_GROUND;
        return;
    case ST_CSI:
        if (c >= '0' && c <= '9') {
            if (v->nparams == 0) v->nparams = 1;
            int *p = &v->params[v->nparams - 1];
            *p = *p * 10 + (int)(c - '0');
            if (*p > 32767) *p = 32767;
            return;
        }
        if (c == ';' || c == ':') { if (v->nparams < MAX_PARAMS) { v->nparams++; v->params[v->nparams - 1] = 0; } return; }
        if (c == '?') { v->priv = true; return; }
        if (c == '>' || c == '<' || c == '=' || c == '!' || c == '"' || c == '$' || c == '\'' || c == ' ') { v->priv = v->priv || (c != ' '); if (c != '?') { /* ignore intermediates, mark to skip */ } return; }
        if (c >= 0x40 && c <= 0x7E) { if (v->nparams == 0 || (v->nparams == 1 && v->params[0] == 0 && c != 'm' && c != 'J' && c != 'K' && c != 'H' && c != 'f' && c != 'r' && c != 'c' && c != 'n' && c != 'g' && c != 'h' && c != 'l' && c != 's' && c != 'u' && c != 't')) { /* keep */ }
            do_csi(v, (char)c); v->state = ST_GROUND; return; }
        if (c < 0x20) { handle_ctrl(v, c); return; }
        v->state = ST_GROUND;
        return;
    case ST_OSC:
        if (c == 0x07) { do_osc(v); v->state = ST_GROUND; return; }
        if (c == 0x1B) { v->state = ST_DCS; do_osc(v); return; }  /* ESC \ terminator */
        if (v->osc_len < OSC_MAX - 1) { if (c < 0x80) v->osc[v->osc_len++] = (char)c; else v->osc[v->osc_len++] = '?'; }
        return;
    case ST_DCS:
        if (c == 0x1B) return;
        if (c == '\\' || c == 0x07) v->state = ST_GROUND;
        return;
    }
}

void vt_feed(vt_t *v, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        if (v->utf8_left) {
            if ((b & 0xC0) == 0x80) {
                v->utf8_cp = (v->utf8_cp << 6) | (b & 0x3F);
                if (--v->utf8_left == 0) process(v, v->utf8_cp);
                continue;
            }
            v->utf8_left = 0;
            process(v, 0xFFFD);
        }
        if (b < 0x80) process(v, b);
        else if ((b & 0xE0) == 0xC0) { v->utf8_cp = b & 0x1F; v->utf8_left = 1; }
        else if ((b & 0xF0) == 0xE0) { v->utf8_cp = b & 0x0F; v->utf8_left = 2; }
        else if ((b & 0xF8) == 0xF0) { v->utf8_cp = b & 0x07; v->utf8_left = 3; }
        else process(v, 0xFFFD);
    }
}

/* ------------------------------------------------------------ lifecycle */
static void *big_alloc(size_t n)
{
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
    if (!p) p = calloc(1, n);
    return p;
}

void vt_reset(vt_t *v)
{
    v->fg = v->bg = VT_COLOR_DEFAULT; v->attr = 0;
    v->cursor_visible = true; v->autowrap = true; v->origin_mode = false; v->insert_mode = false;
    v->app_cursor = false; v->app_keypad = false; v->bracketed_paste = false; v->mouse = false;
    v->scroll_top = 0; v->scroll_bot = v->rows - 1;
    v->charset_g0 = 0; v->state = ST_GROUND; v->utf8_left = 0;
    v->screen = v->main; v->alt_active = false;
    memset(v->tabs, 0, sizeof(v->tabs));
    for (int x = 8; x < v->cols && x < 512; x += 8) v->tabs[x >> 3] |= 1 << (x & 7);
    clear_rows(v, 0, v->rows);
    v->cx = v->cy = 0; v->wrap_pending = false;
    mark_all(v);
}

vt_t *vt_create(int cols, int rows, int scrollback)
{
    vt_t *v = calloc(1, sizeof(*v));
    v->cols = cols; v->rows = rows;
    v->main = big_alloc(sizeof(vt_cell_t) * cols * rows);
    v->alt = big_alloc(sizeof(vt_cell_t) * cols * rows);
    v->dirty = calloc(rows, 1);
    v->sb_cap = scrollback;
    if (scrollback > 0) v->sb = big_alloc(sizeof(vt_cell_t) * cols * scrollback);
    v->screen = v->main;
    vt_reset(v);
    return v;
}

void vt_destroy(vt_t *v)
{
    if (!v) return;
    free(v->main); free(v->alt); free(v->sb); free(v->dirty); free(v);
}

void vt_resize(vt_t *v, int cols, int rows)
{
    if (cols == v->cols && rows == v->rows) return;
    if (cols < 2 || rows < 1) return;
    vt_cell_t *nm = big_alloc(sizeof(vt_cell_t) * cols * rows);
    vt_cell_t *na = big_alloc(sizeof(vt_cell_t) * cols * rows);
    for (int i = 0; i < cols * rows; i++) { nm[i].cp = ' '; nm[i].fg = nm[i].bg = VT_COLOR_DEFAULT; na[i] = nm[i]; }
    /* keep bottom rows when shrinking */
    int copy_rows = rows < v->rows ? rows : v->rows;
    int src_start = (v->rows > rows) ? v->rows - rows : 0;
    if (v->cy < src_start) src_start = v->cy;   /* keep cursor row visible */
    if (src_start + copy_rows > v->rows) copy_rows = v->rows - src_start;
    for (int y = 0; y < src_start; y++) sb_push(v, &v->main[y * v->cols]);
    int cc = cols < v->cols ? cols : v->cols;
    for (int y = 0; y < copy_rows; y++) {
        memcpy(&nm[y * cols], &v->main[(y + src_start) * v->cols], sizeof(vt_cell_t) * cc);
        memcpy(&na[y * cols], &v->alt[(y + src_start) * v->cols], sizeof(vt_cell_t) * cc);
    }
    /* rebuild scrollback with new width */
    if (v->sb) {
        vt_cell_t *nsb = big_alloc(sizeof(vt_cell_t) * cols * v->sb_cap);
        for (int i = 0; i < v->sb_cap * cols; i++) { nsb[i].cp = ' '; nsb[i].fg = nsb[i].bg = VT_COLOR_DEFAULT; }
        for (int i = 0; i < v->sb_used; i++) {
            int src = (v->sb_head - v->sb_used + i + v->sb_cap) % v->sb_cap;
            memcpy(&nsb[i * cols], &v->sb[src * v->cols], sizeof(vt_cell_t) * cc);
        }
        free(v->sb); v->sb = nsb; v->sb_head = v->sb_used % v->sb_cap;
    }
    bool alt = v->alt_active;
    free(v->main); free(v->alt); free(v->dirty);
    v->main = nm; v->alt = na; v->screen = alt ? na : nm;
    v->dirty = calloc(rows, 1);
    v->cols = cols; v->rows = rows;
    v->cy -= src_start; if (v->cy >= rows) v->cy = rows - 1; if (v->cy < 0) v->cy = 0;
    if (v->cx >= cols) v->cx = cols - 1;
    v->scroll_top = 0; v->scroll_bot = rows - 1;
    v->view_off = 0;
    mark_all(v);
}

int vt_cols(const vt_t *v) { return v->cols; }
int vt_rows(const vt_t *v) { return v->rows; }

const vt_cell_t *vt_view_row(vt_t *v, int row)
{
    if (v->view_off == 0 || v->alt_active) return &v->screen[row * v->cols];
    int off = v->view_off;
    if (row < off) {
        int idx = v->sb_used - off + row;   /* 0..sb_used-1 oldest..newest */
        if (idx < 0) idx = 0;
        int src = (v->sb_head - v->sb_used + idx + v->sb_cap) % v->sb_cap;
        return &v->sb[src * v->cols];
    }
    return &v->screen[(row - off) * v->cols];
}

bool vt_row_dirty(vt_t *v, int row) { return v->dirty[row]; }
void vt_clear_dirty(vt_t *v) { memset(v->dirty, 0, v->rows); }
void vt_mark_all_dirty(vt_t *v) { mark_all(v); }
void vt_cursor(const vt_t *v, int *x, int *y, bool *visible)
{
    if (x) *x = v->cx; if (y) *y = v->cy + v->view_off;
    if (visible) *visible = v->cursor_visible && (v->view_off == 0);
}
int vt_view_offset(const vt_t *v) { return v->view_off; }
int vt_scrollback_used(const vt_t *v) { return v->sb_used; }
void vt_scroll_view(vt_t *v, int delta)
{
    if (v->alt_active) return;
    int n = v->view_off + delta;
    if (n < 0) n = 0; if (n > v->sb_used) n = v->sb_used;
    if (n != v->view_off) { v->view_off = n; mark_all(v); }
}
void vt_scroll_view_reset(vt_t *v) { if (v->view_off) { v->view_off = 0; mark_all(v); } }
bool vt_app_cursor(const vt_t *v) { return v->app_cursor; }
bool vt_app_keypad(const vt_t *v) { return v->app_keypad; }
bool vt_bracketed_paste(const vt_t *v) { return v->bracketed_paste; }
bool vt_mouse_enabled(const vt_t *v) { return v->mouse; }
void vt_set_write_cb(vt_t *v, vt_write_cb_t cb, void *user) { v->write_cb = cb; v->write_user = user; }
void vt_set_bell_cb(vt_t *v, vt_bell_cb_t cb, void *user) { v->bell_cb = cb; v->bell_user = user; }
void vt_set_title_cb(vt_t *v, vt_title_cb_t cb, void *user) { v->title_cb = cb; v->title_user = user; }
const char *vt_title(const vt_t *v) { return v->title; }

size_t vt_row_text(vt_t *v, int row, char *buf, size_t cap)
{
    const vt_cell_t *r = vt_view_row(v, row);
    size_t o = 0;
    for (int x = 0; x < v->cols && o + 5 < cap; x++) {
        uint32_t c = r[x].cp;
        if (r[x].attr & VT_ATTR_WIDE_CONT) continue;
        if (c == 0) c = ' ';
        if (c < 0x80) buf[o++] = (char)c;
        else if (c < 0x800) { buf[o++] = 0xC0 | (c >> 6); buf[o++] = 0x80 | (c & 0x3F); }
        else if (c < 0x10000) { buf[o++] = 0xE0 | (c >> 12); buf[o++] = 0x80 | ((c >> 6) & 0x3F); buf[o++] = 0x80 | (c & 0x3F); }
        else { buf[o++] = 0xF0 | (c >> 18); buf[o++] = 0x80 | ((c >> 12) & 0x3F); buf[o++] = 0x80 | ((c >> 6) & 0x3F); buf[o++] = 0x80 | (c & 0x3F); }
    }
    while (o > 0 && buf[o - 1] == ' ') o--;
    buf[o] = 0;
    return o;
}

void vt_set_palette(const uint32_t pal[16])
{
    for (int i = 0; i < 16; i++) s_ansi16[i] = pal[i] & 0xFFFFFF;
}
