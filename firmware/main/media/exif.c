#include "exif.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static bool s_le;
static uint16_t rd16(const uint8_t *p) { return s_le ? (p[0] | (p[1] << 8)) : ((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t *p) { return s_le ? (p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)) : (((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); }

static double rational(const uint8_t *t, size_t tlen, uint32_t off)
{
    if (off + 8 > tlen) return 0;
    uint32_t n = rd32(t + off), d = rd32(t + off + 4);
    return d ? (double)n / d : 0;
}
static void copy_str(char *dst, size_t cap, const uint8_t *t, size_t tlen, uint32_t off, uint32_t cnt)
{
    if (cnt > cap - 1) cnt = cap - 1;
    if (off + cnt > tlen) return;
    memcpy(dst, t + off, cnt); dst[cnt] = 0;
    for (int i = (int)cnt - 1; i >= 0 && (dst[i] == ' ' || dst[i] == 0); i--) dst[i] = 0;
}

static void parse_ifd(const uint8_t *t, size_t tlen, uint32_t ifd, exif_info_t *e, int kind, int depth)
{
    if (depth > 3 || ifd + 2 > tlen) return;
    uint16_t n = rd16(t + ifd);
    if (n > 200) return;
    for (uint16_t i = 0; i < n; i++) {
        uint32_t ent = ifd + 2 + i * 12;
        if (ent + 12 > tlen) return;
        uint16_t tag = rd16(t + ent), type = rd16(t + ent + 2);
        uint32_t cnt = rd32(t + ent + 4);
        uint32_t sz = (type == 3 ? 2 : (type == 4 || type == 9) ? 4 : (type == 5 || type == 10) ? 8 : 1) * cnt;
        uint32_t voff = sz <= 4 ? ent + 8 : rd32(t + ent + 8);
        uint32_t v = type == 3 ? rd16(t + ent + 8) : rd32(t + ent + 8);
        if (kind == 0) {
            switch (tag) {
            case 0x010F: copy_str(e->make, sizeof(e->make), t, tlen, voff, cnt); break;
            case 0x0110: copy_str(e->model, sizeof(e->model), t, tlen, voff, cnt); break;
            case 0x0112: e->orientation = v; break;
            case 0x0131: copy_str(e->software, sizeof(e->software), t, tlen, voff, cnt); break;
            case 0x0132: if (!e->datetime[0]) copy_str(e->datetime, sizeof(e->datetime), t, tlen, voff, cnt); break;
            case 0x8769: parse_ifd(t, tlen, v, e, 1, depth + 1); break;
            case 0x8825: parse_ifd(t, tlen, v, e, 2, depth + 1); break;
            }
        } else if (kind == 1) {
            switch (tag) {
            case 0x829A: e->exposure = rational(t, tlen, voff); break;
            case 0x829D: e->fnumber = rational(t, tlen, voff); break;
            case 0x8827: e->iso = v; break;
            case 0x9003: copy_str(e->datetime, sizeof(e->datetime), t, tlen, voff, cnt); break;
            case 0x920A: e->focal_mm = rational(t, tlen, voff); break;
            case 0xA002: e->width = v; break;
            case 0xA003: e->height = v; break;
            case 0xA434: copy_str(e->lens, sizeof(e->lens), t, tlen, voff, cnt); break;
            }
        } else if (kind == 2) {
            static char ref_lat = 'N', ref_lon = 'E';
            switch (tag) {
            case 0x0001: ref_lat = t[voff]; break;
            case 0x0003: ref_lon = t[voff]; break;
            case 0x0002: case 0x0004: {
                double d = rational(t, tlen, voff) + rational(t, tlen, voff + 8) / 60.0 + rational(t, tlen, voff + 16) / 3600.0;
                if (tag == 0x0002) e->lat = (ref_lat == 'S') ? -d : d; else e->lon = (ref_lon == 'W') ? -d : d;
                e->has_gps = true; break; }
            }
        }
    }
}

bool exif_parse_file(const char *path, exif_info_t *out)
{
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t hdr[4];
    if (fread(hdr, 1, 2, f) != 2 || hdr[0] != 0xFF || hdr[1] != 0xD8) { fclose(f); return false; }
    bool ok = false;
    for (int guard = 0; guard < 32; guard++) {
        if (fread(hdr, 1, 4, f) != 4 || hdr[0] != 0xFF) break;
        uint8_t marker = hdr[1]; uint16_t len = (hdr[2] << 8) | hdr[3];
        if (marker == 0xDA || marker == 0xD9) break;
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            uint8_t sof[5]; if (fread(sof, 1, 5, f) == 5) { out->height = out->height ? out->height : ((sof[1] << 8) | sof[2]); out->width = out->width ? out->width : ((sof[3] << 8) | sof[4]); }
            fseek(f, len - 2 - 5, SEEK_CUR); continue;
        }
        if (marker == 0xE1 && len > 8) {
            uint8_t *seg = malloc(len - 2);
            if (fread(seg, 1, len - 2, f) != (size_t)(len - 2)) { free(seg); break; }
            if (!memcmp(seg, "Exif\0\0", 6)) {
                const uint8_t *t = seg + 6; size_t tlen = len - 8;
                if (tlen > 8 && (t[0] == 'I' || t[0] == 'M')) {
                    s_le = (t[0] == 'I');
                    uint32_t ifd0 = rd32(t + 4);
                    parse_ifd(t, tlen, ifd0, out, 0, 0);
                    ok = true;
                }
            }
            free(seg);
            continue;
        }
        fseek(f, len - 2, SEEK_CUR);
    }
    fclose(f);
    out->valid = ok;
    return ok;
}

const char *exif_describe(const exif_info_t *e, char *out, int cap)
{
    int n = 0;
    out[0] = 0;
    if (e->width && e->height) n += snprintf(out + n, cap - n, "%lux%lu\n", (unsigned long)e->width, (unsigned long)e->height);
    if (!e->valid) return out;
    if (e->make[0] || e->model[0]) n += snprintf(out + n, cap - n, "%s %s\n", e->make, e->model);
    if (e->lens[0]) n += snprintf(out + n, cap - n, "%s\n", e->lens);
    if (e->datetime[0]) n += snprintf(out + n, cap - n, "%s\n", e->datetime);
    if (e->exposure > 0) {
        if (e->exposure < 1) n += snprintf(out + n, cap - n, "1/%.0fs  ", 1.0 / e->exposure); else n += snprintf(out + n, cap - n, "%.1fs  ", e->exposure);
    }
    if (e->fnumber > 0) n += snprintf(out + n, cap - n, "f/%.1f  ", e->fnumber);
    if (e->iso) n += snprintf(out + n, cap - n, "ISO %d  ", e->iso);
    if (e->focal_mm > 0) n += snprintf(out + n, cap - n, "%.0fmm", e->focal_mm);
    if (n && out[n - 1] == ' ') { while (n && out[n - 1] == ' ') n--; out[n++] = '\n'; out[n] = 0; }
    if (e->orientation) n += snprintf(out + n, cap - n, "Orientation %d\n", e->orientation);
    if (e->has_gps) n += snprintf(out + n, cap - n, "GPS %.5f, %.5f\n", e->lat, e->lon);
    if (e->software[0]) n += snprintf(out + n, cap - n, "%s\n", e->software);
    if (n && out[n - 1] == '\n') out[n - 1] = 0;
    return out;
}
