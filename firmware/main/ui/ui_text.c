#include "ui.h"
#include "storage/sd.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TEXT_MAX (256 * 1024)

/* Best-effort conversion of UTF-16 with BOM to UTF-8; otherwise return as-is. */
static char *normalize(char *buf, size_t len)
{
    const unsigned char *u = (const unsigned char *)buf;
    bool le = len >= 2 && u[0] == 0xFF && u[1] == 0xFE, be = len >= 2 && u[0] == 0xFE && u[1] == 0xFF;
    if (!le && !be) {
        if (len >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) memmove(buf, buf + 3, len - 2);
        return buf;
    }
    char *out = malloc(len * 2 + 1); size_t o = 0;
    for (size_t i = 2; i + 1 < len; i += 2) {
        uint32_t c = le ? (u[i] | (u[i + 1] << 8)) : ((u[i] << 8) | u[i + 1]);
        if (c >= 0xD800 && c <= 0xDBFF && i + 3 < len) { uint32_t lo = le ? (u[i + 2] | (u[i + 3] << 8)) : ((u[i + 2] << 8) | u[i + 3]); c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); i += 2; }
        if (c < 0x80) out[o++] = c;
        else if (c < 0x800) { out[o++] = 0xC0 | (c >> 6); out[o++] = 0x80 | (c & 0x3F); }
        else if (c < 0x10000) { out[o++] = 0xE0 | (c >> 12); out[o++] = 0x80 | ((c >> 6) & 0x3F); out[o++] = 0x80 | (c & 0x3F); }
        else { out[o++] = 0xF0 | (c >> 18); out[o++] = 0x80 | ((c >> 12) & 0x3F); out[o++] = 0x80 | ((c >> 6) & 0x3F); out[o++] = 0x80 | (c & 0x3F); }
    }
    out[o] = 0; free(buf); return out;
}

lv_obj_t *ui_text_create(void *arg)
{
    const char *path = arg;
    lv_obj_t *scr = ui_screen_base();
    ui_topbar(scr, path ? ui_basename(path) : tr(STR_TEXT_VIEWER), true);
    lv_obj_t *c = ui_content(scr);
    char *buf = NULL; size_t len = 0;
    if (!path || !fs_read_all(path, &buf, &len, TEXT_MAX)) {
        ui_label(c, fs_exists(path ? path : "") ? tr(STR_FILE_TOO_LARGE) : tr(STR_CANNOT_READ), FONT_BODY, UI_WARN);
        return scr;
    }
    buf = normalize(buf, len);
    /* replace NULs so the label shows everything */
    for (size_t i = 0; i < strlen(buf); i++) if ((unsigned char)buf[i] < 0x09) buf[i] = ' ';
    lv_obj_t *l = lv_label_create(c);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(l, &g_term_font, 0);
    lv_label_set_text(l, buf);
    free(buf);
    char info[64]; ui_format_size(info, sizeof(info), len);
    ui_label(c, info, FONT_SMALL, UI_MUTED);
    return scr;
}
