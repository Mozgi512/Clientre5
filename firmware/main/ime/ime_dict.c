#include "ime.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "imedict";

struct ime_dict {
    const uint8_t *data;    /* IMD1 blob */
    size_t len;
    bool owned;
    uint32_t count;
    const uint32_t *offs;
    /* scratch for lookup results */
    char *scratch; size_t scratch_cap;
};

ime_dict_t *ime_dict_open_mem(const uint8_t *data, size_t len)
{
    if (len < 8 || memcmp(data, "IMD1", 4)) return NULL;
    ime_dict_t *d = calloc(1, sizeof(*d));
    d->data = data; d->len = len;
    memcpy(&d->count, data + 4, 4);
    d->offs = (const uint32_t *)(data + 8);
    if (8 + (size_t)d->count * 4 > len) { free(d); return NULL; }
    d->scratch_cap = 4096;
    d->scratch = malloc(d->scratch_cap);
    return d;
}

void ime_dict_close(ime_dict_t *d)
{
    if (!d) return;
    if (d->owned) free((void *)d->data);
    free(d->scratch);
    free(d);
}

int ime_dict_count(ime_dict_t *d) { return d ? (int)d->count : 0; }

static const char *entry_key(ime_dict_t *d, uint32_t i) { return (const char *)(d->data + d->offs[i]); }

int ime_dict_lookup(ime_dict_t *d, const char *reading, const char **out, int max)
{
    if (!d || !reading || !*reading) return 0;
    int lo = 0, hi = (int)d->count - 1, found = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(entry_key(d, mid), reading);
        if (c == 0) { found = mid; break; }
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    if (found < 0) return 0;
    const char *k = entry_key(d, found);
    const char *cands = k + strlen(k) + 1;
    size_t cl = strlen(cands);
    if (cl + 1 > d->scratch_cap) { d->scratch_cap = cl + 1; d->scratch = realloc(d->scratch, d->scratch_cap); }
    memcpy(d->scratch, cands, cl + 1);
    int n = 0;
    char *p = d->scratch;
    while (p && *p && n < max) {
        char *sep = strchr(p, 0x1f);
        if (sep) *sep = 0;
        out[n++] = p;
        p = sep ? sep + 1 : NULL;
    }
    return n;
}

/* ---- SKK text import (EUC-JP or UTF-8) -> IMD1 in PSRAM ---- */
static int euc_to_utf8(const uint8_t *in, size_t n, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < n && o + 4 < cap;) {
        uint8_t c = in[i];
        uint32_t cp;
        if (c < 0x80) { cp = c; i++; }
        else if (c == 0x8E && i + 1 < n) { cp = 0xFF61 + (in[i + 1] - 0xA1); i += 2; }
        else if (c >= 0xA1 && c <= 0xFE && i + 1 < n) {
            /* JIS X 0208 -> Unicode requires a table; fall back to using the
             * system iconv-less approach: we map via a small heuristic is not
             * possible, so mark unknown. Real conversion below via table. */
            extern int jisx0208_to_ucs(uint8_t hi, uint8_t lo);
            int u = jisx0208_to_ucs(c - 0x80, in[i + 1] - 0x80);
            cp = u > 0 ? (uint32_t)u : 0x3013;
            i += 2;
        } else { i++; continue; }
        if (cp < 0x80) out[o++] = cp;
        else if (cp < 0x800) { out[o++] = 0xC0 | (cp >> 6); out[o++] = 0x80 | (cp & 0x3F); }
        else { out[o++] = 0xE0 | (cp >> 12); out[o++] = 0x80 | ((cp >> 6) & 0x3F); out[o++] = 0x80 | (cp & 0x3F); }
    }
    out[o] = 0;
    return (int)o;
}

typedef struct { char *key; char *cands; } ent_t;
static int ent_cmp(const void *a, const void *b) { return strcmp(((const ent_t *)a)->key, ((const ent_t *)b)->key); }

ime_dict_t *ime_dict_open_skk_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 12 * 1024 * 1024) { fclose(f); return NULL; }
    uint8_t *raw = heap_caps_malloc(sz + 1, MALLOC_CAP_SPIRAM);
    if (!raw) { fclose(f); return NULL; }
    size_t n = fread(raw, 1, sz, f); fclose(f); raw[n] = 0;
    bool utf8 = (strstr((char *)raw, "coding: utf-8") != NULL) || (strstr((char *)raw, "coding: utf8") != NULL);
    /* Count lines */
    size_t cap = 1024, cnt = 0;
    ent_t *ents = heap_caps_malloc(cap * sizeof(ent_t), MALLOC_CAP_SPIRAM);
    char *conv = malloc(8192);
    size_t pos = 0;
    while (pos < n) {
        uint8_t *line = raw + pos;
        uint8_t *nl = memchr(line, '\n', n - pos);
        size_t ll = nl ? (size_t)(nl - line) : n - pos;
        pos += ll + 1;
        if (ll == 0 || line[0] == ';') continue;
        if (ll > 6000) continue;
        if (utf8) { memcpy(conv, line, ll); conv[ll] = 0; }
        else euc_to_utf8(line, ll, conv, 8192);
        char *sp = strchr(conv, ' ');
        if (!sp) continue;
        *sp = 0;
        char *rest = sp + 1;
        /* candidates: /a/b;note/c/ -> a\x1fb\x1fc */
        char *outc = malloc(strlen(rest) + 1); size_t oc = 0;
        char *p = rest;
        if (*p == '/') p++;
        while (*p) {
            char *slash = strchr(p, '/');
            size_t cl = slash ? (size_t)(slash - p) : strlen(p);
            char *semi = memchr(p, ';', cl);
            if (semi) cl = semi - p;
            if (cl > 0 && strncmp(p, "(concat", 7) != 0) {
                if (oc) outc[oc++] = 0x1f;
                memcpy(outc + oc, p, cl); oc += cl;
            }
            if (!slash) break;
            p = slash + 1;
        }
        outc[oc] = 0;
        if (oc == 0) { free(outc); continue; }
        if (cnt == cap) { cap *= 2; ents = heap_caps_realloc(ents, cap * sizeof(ent_t), MALLOC_CAP_SPIRAM); }
        ents[cnt].key = strdup(conv);
        ents[cnt].cands = outc;
        cnt++;
    }
    free(conv);
    free(raw);
    qsort(ents, cnt, sizeof(ent_t), ent_cmp);
    /* serialize */
    size_t total = 8 + cnt * 4;
    for (size_t i = 0; i < cnt; i++) total += strlen(ents[i].key) + 1 + strlen(ents[i].cands) + 1;
    uint8_t *blob = heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!blob) { for (size_t i = 0; i < cnt; i++) { free(ents[i].key); free(ents[i].cands); } free(ents); return NULL; }
    memcpy(blob, "IMD1", 4);
    uint32_t c32 = cnt; memcpy(blob + 4, &c32, 4);
    uint32_t *offs = (uint32_t *)(blob + 8);
    size_t w = 8 + cnt * 4;
    for (size_t i = 0; i < cnt; i++) {
        offs[i] = w;
        size_t kl = strlen(ents[i].key) + 1, cl = strlen(ents[i].cands) + 1;
        memcpy(blob + w, ents[i].key, kl); w += kl;
        memcpy(blob + w, ents[i].cands, cl); w += cl;
        free(ents[i].key); free(ents[i].cands);
    }
    free(ents);
    ime_dict_t *d = ime_dict_open_mem(blob, total);
    if (!d) { free(blob); return NULL; }
    d->owned = true;
    ESP_LOGI(TAG, "loaded %s: %u entries, %u bytes", path, (unsigned)cnt, (unsigned)total);
    return d;
}
