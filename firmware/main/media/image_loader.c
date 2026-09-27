#include "image_loader.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/jpeg_decode.h"
#include "tjpgd/tjpgd.h"
#include "miniz.h"

static const char *TAG = "img";
#define HW_MAX_PIXELS (2200 * 1700)

typedef struct { FILE *f; const uint8_t *mem; size_t mem_len, mem_pos; uint16_t *out; int out_w, out_h; } jd_io_t;

static size_t jd_in(JDEC *jd, uint8_t *buf, size_t n)
{
    jd_io_t *io = jd->device;
    if (io->mem) {
        size_t left = io->mem_len - io->mem_pos; if (n > left) n = left;
        if (buf) memcpy(buf, io->mem + io->mem_pos, n);
        io->mem_pos += n; return n;
    }
    if (buf) return fread(buf, 1, n, io->f);
    fseek(io->f, n, SEEK_CUR); return n;
}
static int jd_out(JDEC *jd, void *bitmap, JRECT *r)
{
    jd_io_t *io = jd->device;
    const uint16_t *src = bitmap;
    int w = r->right - r->left + 1;
    for (int y = r->top; y <= r->bottom; y++) {
        if (y >= io->out_h) break;
        int cw = w; if (r->left + cw > io->out_w) cw = io->out_w - r->left;
        if (cw > 0) memcpy(io->out + y * io->out_w + r->left, src, cw * 2);
        src += w;
    }
    return 1;
}

static lv_image_dsc_t *make_dsc(uint16_t *px, int w, int h)
{
    lv_image_dsc_t *d = calloc(1, sizeof(*d));
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = w; d->header.h = h; d->header.stride = w * 2;
    d->data = (const uint8_t *)px; d->data_size = (uint32_t)w * h * 2;
    return d;
}

static lv_image_dsc_t *sw_decode(jd_io_t *io, int max_w, int max_h, char *status, int cap)
{
    size_t wsz = 8192;
    void *work = malloc(wsz);
    JDEC jd;
    JRESULT r = jd_prepare(&jd, jd_in, work, wsz, io);
    if (r != JDR_OK) { free(work); if (status) snprintf(status, cap, "JPEG header parse failed (%d)", r); return NULL; }
    int scale = 0;
    while (scale < 3 && ((jd.width >> scale) > (unsigned)max_w || (jd.height >> scale) > (unsigned)max_h)) scale++;
    int w = jd.width >> scale, h = jd.height >> scale;
    if (w < 1) w = 1; if (h < 1) h = 1;
    io->out_w = w; io->out_h = h;
    io->out = heap_caps_malloc((size_t)w * h * 2, MALLOC_CAP_SPIRAM);
    if (!io->out) { free(work); if (status) snprintf(status, cap, "JPEG scale preview: no memory"); return NULL; }
    r = jd_decomp(&jd, jd_out, scale);
    free(work);
    if (r != JDR_OK) { free(io->out); if (status) snprintf(status, cap, "JPEG software decode failed (%d)", r); return NULL; }
    if (status) snprintf(status, cap, "JPEG software decode 1/%d", 1 << scale);
    return make_dsc(io->out, w, h);
}

static lv_image_dsc_t *hw_decode(const uint8_t *data, size_t len, uint32_t w, uint32_t h, char *status, int cap)
{
    jpeg_decoder_handle_t dec = NULL;
    jpeg_decode_engine_cfg_t ecfg = { .intr_priority = 0, .timeout_ms = 3000 };
    if (jpeg_new_decoder_engine(&ecfg, &dec) != ESP_OK) return NULL;
    jpeg_decode_cfg_t dcfg = { .output_format = JPEG_DECODE_OUT_FORMAT_RGB565, .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_RGB, .conv_std = JPEG_YUV_RGB_CONV_STD_BT601 };
    jpeg_decode_memory_alloc_cfg_t out_alloc = { .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER };
    jpeg_decode_memory_alloc_cfg_t in_alloc = { .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER };
    uint32_t aw = (w + 15) & ~15u, ah = (h + 15) & ~15u;
    size_t out_sz = 0, in_sz = 0;
    uint8_t *out = jpeg_alloc_decoder_mem((size_t)aw * ah * 2, &out_alloc, &out_sz);
    uint8_t *in = jpeg_alloc_decoder_mem(len, &in_alloc, &in_sz);
    lv_image_dsc_t *res = NULL;
    if (out && in) {
        memcpy(in, data, len);
        uint32_t got = 0;
        esp_err_t e = jpeg_decoder_process(dec, &dcfg, in, len, out, out_sz, &got);
        if (e == ESP_OK) {
            /* copy into a tightly packed PSRAM buffer (decoder output stride is aw) */
            uint16_t *px = heap_caps_malloc((size_t)w * h * 2, MALLOC_CAP_SPIRAM);
            if (px) {
                for (uint32_t y = 0; y < h; y++) memcpy(px + y * w, out + (size_t)y * aw * 2, w * 2);
                res = make_dsc(px, w, h);
                if (status) snprintf(status, cap, "JPEG hardware decode");
            }
        } else if (status) snprintf(status, cap, "JPEG hardware decode failed: %s", esp_err_to_name(e));
    }
    if (out) free(out);
    if (in) free(in);
    jpeg_del_decoder_engine(dec);
    return res;
}

/* ---------------- PNG: streaming decode with box downsampling ----------------
 * LVGL's lodepng decoder inflates a whole PNG into one ARGB8888 buffer, so a
 * 2940x1912 photo wants 22 MB on top of the 7 MB file -- more than the PSRAM
 * left over -- and it runs on the LVGL task, which is why the UI froze. Inflate
 * with the ROM's miniz instead and box-average every scanline straight into an
 * RGB565 buffer already sized for the screen, so only two scanlines are held at
 * a time. Interlaced (Adam7) files are left to LVGL. */

typedef struct {
    int       w, h, depth, ct, spp;
    int       bpp;                 /* filter distance, bytes */
    size_t    stride;              /* bytes per source scanline */
    uint8_t  *cur, *prev;
    size_t    got;                 /* bytes of cur filled so far */
    int       filter;              /* -1 until the scanline's filter byte arrives */
    int       srow;
    uint8_t   pal[256 * 3];
    int       pal_n;
    int       step, ow, oh, orow;  /* box size and destination geometry */
    uint16_t *out;
    uint32_t *acc;                 /* ow * 3 colour accumulators */
    int       acc_rows;
    bool      failed;
} png_t;

typedef struct { FILE *f; uint32_t left; bool done; } png_idat_t;

static uint32_t be32(const uint8_t *b) { return ((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]; }

/* Reads IDAT payload bytes, transparently stepping over chunk headers, other
 * chunks and CRCs. Returns 0 once the image data ends. */
static size_t png_idat_read(png_idat_t *s, uint8_t *buf, size_t n)
{
    while (!s->done) {
        if (s->left == 0) {
            uint8_t hdr[8];
            if (fread(hdr, 1, 8, s->f) != 8) { s->done = true; break; }
            uint32_t len = be32(hdr);
            if (!memcmp(hdr + 4, "IDAT", 4)) s->left = len;
            else if (!memcmp(hdr + 4, "IEND", 4)) { s->done = true; break; }
            else if (fseek(s->f, len + 4, SEEK_CUR) != 0) { s->done = true; break; }
            continue;
        }
        size_t want = n < s->left ? n : s->left;
        size_t got = fread(buf, 1, want, s->f);
        if (got == 0) { s->done = true; break; }
        s->left -= got;
        if (s->left == 0) fseek(s->f, 4, SEEK_CUR);   /* chunk CRC */
        return got;
    }
    return 0;
}

static bool png_header(FILE *f, png_t *p, png_idat_t *s, char *status, int cap)
{
    uint8_t sig[8];
    if (fread(sig, 1, 8, f) != 8 || memcmp(sig, "\x89PNG\r\n\x1a\n", 8)) {
        if (status) snprintf(status, cap, "not a PNG");
        return false;
    }
    for (;;) {
        uint8_t hdr[8];
        if (fread(hdr, 1, 8, f) != 8) { if (status) snprintf(status, cap, "truncated PNG"); return false; }
        uint32_t len = be32(hdr);
        if (!memcmp(hdr + 4, "IHDR", 4)) {
            uint8_t d[13];
            if (len < 13 || fread(d, 1, 13, f) != 13) return false;
            p->w = (int)be32(d); p->h = (int)be32(d + 4);
            p->depth = d[8]; p->ct = d[9];
            if (d[12]) { if (status) snprintf(status, cap, "interlaced PNG"); return false; }
            fseek(f, (long)len - 13 + 4, SEEK_CUR);
        } else if (!memcmp(hdr + 4, "PLTE", 4)) {
            p->pal_n = (int)(len / 3);
            if (p->pal_n > 256) p->pal_n = 256;
            if (fread(p->pal, 1, (size_t)p->pal_n * 3, f) != (size_t)p->pal_n * 3) return false;
            fseek(f, (long)len - p->pal_n * 3 + 4, SEEK_CUR);
        } else if (!memcmp(hdr + 4, "IDAT", 4)) {
            s->f = f; s->left = len;
            return p->w > 0 && p->h > 0;
        } else if (!memcmp(hdr + 4, "IEND", 4)) {
            return false;
        } else {
            fseek(f, (long)len + 4, SEEK_CUR);
        }
    }
}

static void png_unfilter(png_t *p)
{
    uint8_t *c = p->cur;
    const uint8_t *v = p->prev;           /* zeroed for the first row */
    size_t len = p->stride;
    int bpp = p->bpp;
    switch (p->filter) {
    case 0: break;
    case 1: for (size_t i = bpp; i < len; i++) c[i] = (uint8_t)(c[i] + c[i - bpp]); break;
    case 2: for (size_t i = 0; i < len; i++) c[i] = (uint8_t)(c[i] + v[i]); break;
    case 3:
        for (size_t i = 0; i < len; i++) {
            int a = i >= (size_t)bpp ? c[i - bpp] : 0;
            c[i] = (uint8_t)(c[i] + ((a + v[i]) >> 1));
        }
        break;
    default:
        for (size_t i = 0; i < len; i++) {
            int a = i >= (size_t)bpp ? c[i - bpp] : 0;
            int b = v[i];
            int d = i >= (size_t)bpp ? v[i - bpp] : 0;
            int q = a + b - d, pa = abs(q - a), pb = abs(q - b), pd = abs(q - d);
            c[i] = (uint8_t)(c[i] + ((pa <= pb && pa <= pd) ? a : (pb <= pd ? b : d)));
        }
        break;
    }
}

static void png_px(const png_t *p, const uint8_t *row, int x, int *r, int *g, int *b)
{
    int s[4] = { 0, 0, 0, 255 };
    if (p->depth == 8) {
        for (int i = 0; i < p->spp; i++) s[i] = row[x * p->spp + i];
    } else if (p->depth == 16) {
        for (int i = 0; i < p->spp; i++) s[i] = row[(x * p->spp + i) * 2];
    } else {
        int bit = x * p->depth, max = (1 << p->depth) - 1;
        int v = (row[bit >> 3] >> (8 - p->depth - (bit & 7))) & max;
        s[0] = (p->ct == 3) ? v : v * 255 / max;
    }
    switch (p->ct) {
    case 2: *r = s[0]; *g = s[1]; *b = s[2]; break;
    case 3: {
        int i = (s[0] < p->pal_n) ? s[0] : 0;
        *r = p->pal[i * 3]; *g = p->pal[i * 3 + 1]; *b = p->pal[i * 3 + 2];
        break;
    }
    case 4: {   /* grey + alpha, composited over white */
        int a = s[1];
        *r = *g = *b = (s[0] * a + 255 * (255 - a)) / 255;
        break;
    }
    case 6: {   /* RGBA, composited over white */
        int a = s[3];
        *r = (s[0] * a + 255 * (255 - a)) / 255;
        *g = (s[1] * a + 255 * (255 - a)) / 255;
        *b = (s[2] * a + 255 * (255 - a)) / 255;
        break;
    }
    default: *r = *g = *b = s[0]; break;
    }
}

static void png_row_ready(png_t *p)
{
    png_unfilter(p);
    if (p->orow < p->oh) {
        int cols = p->ow * p->step;
        for (int x = 0; x < cols; x++) {
            int r, g, b;
            png_px(p, p->cur, x, &r, &g, &b);
            uint32_t *a = &p->acc[(x / p->step) * 3];
            a[0] += (uint32_t)r; a[1] += (uint32_t)g; a[2] += (uint32_t)b;
        }
        if (++p->acc_rows == p->step) {
            uint32_t n = (uint32_t)p->step * (uint32_t)p->step;
            uint16_t *dst = p->out + (size_t)p->orow * p->ow;
            for (int o = 0; o < p->ow; o++) {
                uint32_t r = p->acc[o * 3] / n, g = p->acc[o * 3 + 1] / n, b = p->acc[o * 3 + 2] / n;
                dst[o] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            }
            memset(p->acc, 0, (size_t)p->ow * 3 * sizeof(uint32_t));
            p->acc_rows = 0;
            p->orow++;
        }
    }
    uint8_t *t = p->prev; p->prev = p->cur; p->cur = t;
    p->srow++;
    p->got = 0;
    p->filter = -1;
}

static void png_feed(png_t *p, const uint8_t *d, size_t n)
{
    while (n && !p->failed && p->srow < p->h) {
        if (p->filter < 0) {
            p->filter = *d++; n--;
            if (p->filter > 4) { p->failed = true; return; }
            continue;
        }
        size_t want = p->stride - p->got;
        if (want > n) want = n;
        memcpy(p->cur + p->got, d, want);
        p->got += want; d += want; n -= want;
        if (p->got == p->stride) png_row_ready(p);
    }
}

lv_image_dsc_t *image_loader_png(const char *path, int max_w, int max_h, char *status, int cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) { if (status) snprintf(status, cap, "open failed"); return NULL; }

    png_t p; memset(&p, 0, sizeof(p)); p.filter = -1;
    png_idat_t s; memset(&s, 0, sizeof(s));
    tinfl_decompressor *dec = NULL;
    uint8_t *dict = NULL, *in = NULL;
    lv_image_dsc_t *res = NULL;

    if (!png_header(f, &p, &s, status, cap)) goto out;

    switch (p.ct) {
    case 0: p.spp = 1; if (p.depth != 1 && p.depth != 2 && p.depth != 4 && p.depth != 8 && p.depth != 16) goto unsupported; break;
    case 2: p.spp = 3; if (p.depth != 8 && p.depth != 16) goto unsupported; break;
    case 3: p.spp = 1; if (p.pal_n == 0 || (p.depth != 1 && p.depth != 2 && p.depth != 4 && p.depth != 8)) goto unsupported; break;
    case 4: p.spp = 2; if (p.depth != 8 && p.depth != 16) goto unsupported; break;
    case 6: p.spp = 4; if (p.depth != 8 && p.depth != 16) goto unsupported; break;
    default: goto unsupported;
    }
    {
        int bits = p.spp * p.depth;
        p.stride = ((size_t)p.w * bits + 7) / 8;
        p.bpp = (bits + 7) / 8;
    }
    p.step = 1;
    while (p.w / p.step > max_w || p.h / p.step > max_h) p.step++;
    p.ow = p.w / p.step; p.oh = p.h / p.step;
    if (p.ow < 1 || p.oh < 1) goto unsupported;

    p.cur = heap_caps_calloc(1, p.stride, MALLOC_CAP_SPIRAM);
    p.prev = heap_caps_calloc(1, p.stride, MALLOC_CAP_SPIRAM);
    p.acc = heap_caps_calloc((size_t)p.ow * 3, sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    p.out = heap_caps_malloc((size_t)p.ow * p.oh * 2, MALLOC_CAP_SPIRAM);
    dec = malloc(sizeof(*dec));
    dict = heap_caps_malloc(TINFL_LZ_DICT_SIZE, MALLOC_CAP_SPIRAM);
    in = malloc(4096);
    if (!p.cur || !p.prev || !p.acc || !p.out || !dec || !dict || !in) {
        if (status) snprintf(status, cap, "PNG %dx%d: no memory", p.w, p.h);
        goto out;
    }

    tinfl_init(dec);
    size_t dict_ofs = 0, in_avail = 0, in_pos = 0;
    bool in_eof = false;
    while (p.orow < p.oh && !p.failed) {
        if (in_pos == in_avail && !in_eof) {
            in_avail = png_idat_read(&s, in, 4096);
            in_pos = 0;
            if (in_avail == 0) in_eof = true;
        }
        size_t in_bytes = in_avail - in_pos;
        size_t out_bytes = TINFL_LZ_DICT_SIZE - dict_ofs;
        tinfl_status st = tinfl_decompress(dec, in + in_pos, &in_bytes, dict, dict + dict_ofs, &out_bytes,
                                           TINFL_FLAG_PARSE_ZLIB_HEADER | (in_eof ? 0 : TINFL_FLAG_HAS_MORE_INPUT));
        in_pos += in_bytes;
        if (out_bytes) png_feed(&p, dict + dict_ofs, out_bytes);
        dict_ofs = (dict_ofs + out_bytes) & (TINFL_LZ_DICT_SIZE - 1);
        if (st <= TINFL_STATUS_DONE) break;                              /* finished or broken stream */
        if (st == TINFL_STATUS_NEEDS_MORE_INPUT && in_eof) break;
    }
    if (p.orow > 0) {
        res = make_dsc(p.out, p.ow, p.orow);
        p.out = NULL;
        if (status) snprintf(status, cap, "PNG %dx%d decoded to %dx%d (1/%d)", p.w, p.h, p.ow, p.orow, p.step);
    } else if (status) {
        snprintf(status, cap, "PNG decode failed");
    }
    goto out;

unsupported:
    if (status) snprintf(status, cap, "PNG colour type %d/%d bit unsupported", p.ct, p.depth);
out:
    free(dec); free(dict); free(in);
    free(p.cur); free(p.prev); free(p.acc); free(p.out);
    fclose(f);
    return res;
}

lv_image_dsc_t *image_loader_jpeg(const char *path, int max_w, int max_h, char *status, int cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) { if (status) snprintf(status, cap, "open failed"); return NULL; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    /* header for size */
    jd_io_t io = { .f = f };
    uint32_t w = 0, h = 0;
    {
        void *work = malloc(4096); JDEC jd;
        if (jd_prepare(&jd, jd_in, work, 4096, &io) == JDR_OK) { w = jd.width; h = jd.height; }
        free(work);
        fseek(f, 0, SEEK_SET);
    }
    lv_image_dsc_t *res = NULL;
    if (w && h && (uint64_t)w * h <= HW_MAX_PIXELS && w <= (uint32_t)max_w * 2 && h <= (uint32_t)max_h * 2 && sz < 6 * 1024 * 1024) {
        uint8_t *buf = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
        if (buf && fread(buf, 1, sz, f) == (size_t)sz) res = hw_decode(buf, sz, w, h, status, cap);
        free(buf);
        fseek(f, 0, SEEK_SET);
    }
    if (!res) res = sw_decode(&io, max_w, max_h, status, cap);
    fclose(f);
    return res;
}

lv_image_dsc_t *image_loader_jpeg_mem(const uint8_t *data, size_t len, int max_w, int max_h)
{
    jd_io_t io = { .mem = data, .mem_len = len };
    return sw_decode(&io, max_w, max_h, NULL, 0);
}

void image_loader_free(lv_image_dsc_t *dsc)
{
    if (!dsc) return;
    free((void *)dsc->data);
    free(dsc);
}
