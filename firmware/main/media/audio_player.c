/* Background music player: MP3 / FLAC / WAV via esp_audio_codec simple decoder -> ES8388. */
#include "audio_player.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "bsp/m5stack_tab5.h"
#include "esp_codec_dev.h"
#include "decoder/esp_audio_dec_default.h"
#include "simple_dec/esp_audio_simple_dec.h"
#include "simple_dec/esp_audio_simple_dec_default.h"
#include "settings/settings.h"
#include "storage/sd.h"

static const char *TAG = "audio";
static const char *ui_basename_audio(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }

static esp_codec_dev_handle_t s_spk = NULL;
static volatile ap_state_t s_state = AP_STOPPED;
static char s_path[300];
static ap_info_t s_info;
static ap_event_cb_t s_cb; static void *s_cb_user;
static volatile bool s_req_stop, s_req_pause;
static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_lock;
static char s_err[96];
static volatile int s_volume = 60;
static int s_applied_volume = -1;
static volatile TickType_t s_volume_changed_tick;
static bool s_codec_open = false;

#define VOLUME_SETTLE_MS 120

static void emit(ap_state_t st) { s_state = st; if (s_cb) s_cb(st, s_cb_user); }

static esp_audio_simple_dec_type_t type_for(const char *path)
{
    const char *x = fs_ext(path);
    if (!strcmp(x, "mp3")) return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    if (!strcmp(x, "flac")) return ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC;
    if (!strcmp(x, "wav")) return ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
    if (!strcmp(x, "aac")) return ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
    if (!strcmp(x, "m4a")) return ESP_AUDIO_SIMPLE_DEC_TYPE_M4A;
    return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
}

/* ---------------- metadata ---------------- */
static uint32_t syncsafe(const uint8_t *p) { return ((p[0] & 0x7F) << 21) | ((p[1] & 0x7F) << 14) | ((p[2] & 0x7F) << 7) | (p[3] & 0x7F); }

static void id3_text(const uint8_t *d, uint32_t n, char *out, size_t cap)
{
    if (n < 2) { out[0] = 0; return; }
    uint8_t enc = d[0]; d++; n--;
    if (enc == 0 || enc == 3) { size_t c = n < cap - 1 ? n : cap - 1; memcpy(out, d, c); out[c] = 0; }
    else { /* UTF-16 */
        bool le = true; if (n >= 2 && d[0] == 0xFE && d[1] == 0xFF) { le = false; d += 2; n -= 2; } else if (n >= 2 && d[0] == 0xFF && d[1] == 0xFE) { d += 2; n -= 2; }
        size_t o = 0;
        for (uint32_t i = 0; i + 1 < n && o + 4 < cap; i += 2) {
            uint32_t c = le ? (d[i] | (d[i + 1] << 8)) : ((d[i] << 8) | d[i + 1]);
            if (!c) break;
            if (c < 0x80) out[o++] = c; else if (c < 0x800) { out[o++] = 0xC0 | (c >> 6); out[o++] = 0x80 | (c & 0x3F); }
            else { out[o++] = 0xE0 | (c >> 12); out[o++] = 0x80 | ((c >> 6) & 0x3F); out[o++] = 0x80 | (c & 0x3F); }
        }
        out[o] = 0;
    }
    for (int i = (int)strlen(out) - 1; i >= 0 && (out[i] == ' ' || out[i] == '\n'); i--) out[i] = 0;
}

/* Parses ID3v2 (title/artist/album, APIC) or FLAC metadata (VORBIS_COMMENT, PICTURE).
 * If want_pic, returns the picture bytes. Returns offset of audio data. */
static long scan_meta(const char *path, ap_info_t *info, bool want_pic, uint8_t **pic, size_t *pic_len, char *mime, size_t mime_cap)
{
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    uint8_t h[10]; long audio_off = 0;
    if (fread(h, 1, 10, f) == 10 && !memcmp(h, "ID3", 3)) {
        uint32_t size = syncsafe(h + 6); uint8_t ver = h[3];
        audio_off = 10 + size;
        uint32_t pos = 0;
        while (pos + 10 <= size) {
            uint8_t fh[10]; if (fread(fh, 1, 10, f) != 10) break;
            if (!fh[0]) break;
            uint32_t fsz = ver >= 4 ? syncsafe(fh + 4) : ((fh[4] << 24) | (fh[5] << 16) | (fh[6] << 8) | fh[7]);
            pos += 10;
            if (fsz == 0 || pos + fsz > size) break;
            bool is_pic = !memcmp(fh, "APIC", 4);
            if (!memcmp(fh, "TIT2", 4) || !memcmp(fh, "TPE1", 4) || !memcmp(fh, "TALB", 4) || (is_pic && want_pic)) {
                uint8_t *d = malloc(fsz);
                if (!d || fread(d, 1, fsz, f) != fsz) { free(d); break; }
                if (is_pic) {
                    uint32_t i = 1; while (i < fsz && d[i]) i++;   /* mime */
                    if (mime) { size_t ml = i - 1 < mime_cap - 1 ? i - 1 : mime_cap - 1; memcpy(mime, d + 1, ml); mime[ml] = 0; }
                    i++; i++; /* NUL + picture type */
                    if (d[0] == 0 || d[0] == 3) { while (i < fsz && d[i]) i++; i++; } else { while (i + 1 < fsz && (d[i] || d[i + 1])) i += 2; i += 2; }
                    if (i < fsz && pic) { *pic_len = fsz - i; *pic = malloc(*pic_len); memcpy(*pic, d + i, *pic_len); }
                } else if (info) {
                    if (!memcmp(fh, "TIT2", 4)) id3_text(d, fsz, info->title, sizeof(info->title));
                    else if (!memcmp(fh, "TPE1", 4)) id3_text(d, fsz, info->artist, sizeof(info->artist));
                    else id3_text(d, fsz, info->album, sizeof(info->album));
                }
                free(d);
            } else fseek(f, fsz, SEEK_CUR);
            pos += fsz;
        }
    } else if (!memcmp(h, "fLaC", 4)) {
        fseek(f, 4, SEEK_SET);
        bool last = false;
        while (!last) {
            uint8_t bh[4]; if (fread(bh, 1, 4, f) != 4) break;
            last = bh[0] & 0x80; uint8_t type = bh[0] & 0x7F; uint32_t len = (bh[1] << 16) | (bh[2] << 8) | bh[3];
            if (type == 4 && info) { /* VORBIS_COMMENT */
                uint8_t *d = malloc(len); if (!d || fread(d, 1, len, f) != len) { free(d); break; }
                uint32_t p = 0; uint32_t vl = d[0] | (d[1] << 8) | (d[2] << 16) | (d[3] << 24); p = 4 + vl;
                if (p + 4 <= len) { uint32_t n = d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | (d[p + 3] << 24); p += 4;
                    for (uint32_t k = 0; k < n && p + 4 <= len; k++) {
                        uint32_t cl = d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | (d[p + 3] << 24); p += 4;
                        if (p + cl > len) break;
                        char *c = malloc(cl + 1); memcpy(c, d + p, cl); c[cl] = 0; p += cl;
                        if (!strncasecmp(c, "TITLE=", 6)) strlcpy(info->title, c + 6, sizeof(info->title));
                        else if (!strncasecmp(c, "ARTIST=", 7)) strlcpy(info->artist, c + 7, sizeof(info->artist));
                        else if (!strncasecmp(c, "ALBUM=", 6)) strlcpy(info->album, c + 6, sizeof(info->album));
                        free(c);
                    } }
                free(d);
            } else if (type == 6 && want_pic && pic) { /* PICTURE */
                uint8_t *d = malloc(len); if (!d || fread(d, 1, len, f) != len) { free(d); break; }
                uint32_t p = 4; uint32_t ml = (d[p] << 24) | (d[p + 1] << 16) | (d[p + 2] << 8) | d[p + 3]; p += 4;
                if (mime) { size_t c = ml < mime_cap - 1 ? ml : mime_cap - 1; memcpy(mime, d + p, c); mime[c] = 0; }
                p += ml; uint32_t dl = (d[p] << 24) | (d[p + 1] << 16) | (d[p + 2] << 8) | d[p + 3]; p += 4 + dl + 16;
                uint32_t pl = (d[p] << 24) | (d[p + 1] << 16) | (d[p + 2] << 8) | d[p + 3]; p += 4;
                if (p + pl <= len) { *pic_len = pl; *pic = malloc(pl); memcpy(*pic, d + p, pl); }
                free(d);
            } else fseek(f, len, SEEK_CUR);
        }
        audio_off = 0;
    }
    fclose(f);
    return audio_off;
}

bool audio_player_cover(const char *path, uint8_t **data, size_t *len, char *mime, size_t mime_cap)
{
    *data = NULL; *len = 0; if (mime) mime[0] = 0;
    scan_meta(path, NULL, true, data, len, mime, mime_cap);
    return *data != NULL;
}

/* ---------------- playback task ---------------- */
static void apply_pending_volume(bool force)
{
    if (!s_spk || !s_codec_open) return;
    int desired = s_volume;
    if (desired == s_applied_volume) return;
    TickType_t now = xTaskGetTickCount();
    if (!force && (now - s_volume_changed_tick) < pdMS_TO_TICKS(VOLUME_SETTLE_MS)) return;
    if (esp_codec_dev_set_out_vol(s_spk, desired) == ESP_CODEC_DEV_OK)
        s_applied_volume = desired;
}

static bool codec_open(int rate, int ch, int bits)
{
    if (!s_spk) {
        bsp_feature_enable(BSP_FEATURE_SPEAKER, true);
        if (bsp_audio_init(NULL) != ESP_OK) { strlcpy(s_err, "Audio init failed", sizeof(s_err)); return false; }
        s_spk = bsp_audio_codec_speaker_init();
        if (!s_spk) { strlcpy(s_err, "Codec init failed", sizeof(s_err)); return false; }
    }
    if (s_codec_open) esp_codec_dev_close(s_spk);
    esp_codec_dev_sample_info_t fs = { .bits_per_sample = bits, .channel = ch, .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) | (ch > 1 ? ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1) : 0), .sample_rate = rate };
    if (esp_codec_dev_open(s_spk, &fs) != ESP_CODEC_DEV_OK) { strlcpy(s_err, "Output init failed", sizeof(s_err)); return false; }
    s_codec_open = true;
    s_applied_volume = -1;
    apply_pending_volume(true);
    return true;
}

static void play_task(void *arg)
{
    (void)arg;
    esp_audio_simple_dec_handle_t dec = NULL;
    FILE *f = NULL;
    uint8_t *in = NULL, *out = NULL;
    esp_audio_simple_dec_type_t type = type_for(s_path);
    if (type == ESP_AUDIO_SIMPLE_DEC_TYPE_NONE) { strlcpy(s_err, "Bad Format", sizeof(s_err)); emit(AP_ERROR); goto done; }
    memset(&s_info, 0, sizeof(s_info));
    long off = scan_meta(s_path, &s_info, false, NULL, NULL, NULL, 0);
    if (!s_info.title[0]) strlcpy(s_info.title, ui_basename_audio(s_path), sizeof(s_info.title));
    f = fopen(s_path, "rb");
    if (!f) { strlcpy(s_err, "Cannot open audio file", sizeof(s_err)); emit(AP_ERROR); goto done; }
    fseek(f, 0, SEEK_END); long fsize = ftell(f); fseek(f, off, SEEK_SET);
    if (esp_audio_simple_check_audio_type(type) != ESP_AUDIO_ERR_OK) {
        snprintf(s_err, sizeof(s_err), "%s decoder not built in", fs_ext(s_path));
        emit(AP_ERROR); goto done;
    }
    esp_audio_simple_dec_cfg_t cfg = { .dec_type = type };
    esp_audio_err_t de = esp_audio_simple_dec_open(&cfg, &dec);
    if (de != ESP_AUDIO_ERR_OK) { snprintf(s_err, sizeof(s_err), "Decoder init failed (%d)", de); emit(AP_ERROR); goto done; }
    size_t in_cap = 8192, out_cap = 32768;
    in = malloc(in_cap); out = heap_caps_malloc(out_cap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!out) out = malloc(out_cap);
    size_t in_len = 0; bool eof = false, opened = false; uint64_t pcm_bytes = 0;
    emit(AP_PLAYING);
    while (!s_req_stop) {
        if (s_req_pause) {
            if (s_state != AP_PAUSED) emit(AP_PAUSED);
            apply_pending_volume(false);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (s_state == AP_PAUSED) emit(AP_PLAYING);
        if (!eof && in_len < in_cap) { size_t n = fread(in + in_len, 1, in_cap - in_len, f); if (n == 0) eof = true; in_len += n; }
        if (in_len == 0 && eof) break;
        esp_audio_simple_dec_raw_t raw = { .buffer = in, .len = in_len, .eos = eof };
        esp_audio_simple_dec_out_t o = { .buffer = out, .len = out_cap };
        esp_audio_err_t e = esp_audio_simple_dec_process(dec, &raw, &o);
        if (e == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
            out_cap = o.needed_size + 1024; free(out); out = malloc(out_cap); continue;
        }
        if (e != ESP_AUDIO_ERR_OK) {
            if (eof) break;
            /* skip a byte to resync */
            if (raw.consumed == 0) raw.consumed = 1;
        }
        if (o.decoded_size) {
            esp_audio_simple_dec_info_t di;
            if (esp_audio_simple_dec_get_info(dec, &di) == ESP_AUDIO_ERR_OK && (!opened || di.sample_rate != (uint32_t)s_info.sample_rate || di.channel != s_info.channels)) {
                s_info.sample_rate = di.sample_rate; s_info.channels = di.channel; s_info.bitrate = di.bitrate;
                if (!codec_open(di.sample_rate, di.channel, di.bits_per_sample ? di.bits_per_sample : 16)) { emit(AP_ERROR); goto done; }
                opened = true;
                if (di.bitrate && fsize > off) s_info.duration_ms = (uint32_t)((uint64_t)(fsize - off) * 8000 / di.bitrate);
            }
            if (opened) {
                esp_codec_dev_write(s_spk, out, o.decoded_size);
                pcm_bytes += o.decoded_size;
                /* Codec control and PCM writes stay on this task.  Volume slider
                 * events only update the requested value, so they can no longer
                 * interrupt the audio stream from the LVGL task. */
                apply_pending_volume(false);
            }
            if (s_info.sample_rate && s_info.channels) s_info.position_ms = (uint32_t)(pcm_bytes * 1000 / ((uint64_t)s_info.sample_rate * s_info.channels * 2));
        }
        if (raw.consumed > 0 && raw.consumed <= in_len) { memmove(in, in + raw.consumed, in_len - raw.consumed); in_len -= raw.consumed; }
        else if (raw.consumed == 0 && in_len == in_cap) { /* decoder wants more than buffer: grow */ in_cap *= 2; in = realloc(in, in_cap); }
    }
    emit(AP_STOPPED);
done:
    if (dec) esp_audio_simple_dec_close(dec);
    if (f) fclose(f);
    free(in); free(out);
    if (s_codec_open) { esp_codec_dev_close(s_spk); s_codec_open = false; }
    s_applied_volume = -1;
    s_task = NULL;
    vTaskDelete(NULL);
}
void audio_player_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_volume = g_settings.volume;
    /* Two independent registries: esp_audio_simple_dec_register_default() only
     * registers the container parsers (WAV, M4A, TS, OGG), while the codecs
     * themselves -- MP3, FLAC, AAC and friends -- come from
     * esp_audio_dec_register_default(). Without the latter, opening an MP3
     * fails with "decoder not registered". */
    esp_audio_err_t e = esp_audio_dec_register_default();
    if (e != ESP_AUDIO_ERR_OK) ESP_LOGE(TAG, "codec registration failed: %d", e);
    e = esp_audio_simple_dec_register_default();
    if (e != ESP_AUDIO_ERR_OK) ESP_LOGE(TAG, "container registration failed: %d", e);
}

static void wait_task_end(void)
{
    s_req_stop = true;
    for (int i = 0; i < 100 && s_task; i++) vTaskDelay(pdMS_TO_TICKS(20));
}

bool audio_player_play(const char *path)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_task) wait_task_end();
    strlcpy(s_path, path, sizeof(s_path));
    s_req_stop = false; s_req_pause = false; s_err[0] = 0;
    BaseType_t ok = xTaskCreatePinnedToCore(play_task, "audio", 16384, NULL, 6, &s_task, 1);
    xSemaphoreGive(s_lock);
    return ok == pdPASS;
}
void audio_player_pause(void) { s_req_pause = true; }
void audio_player_resume(void) { s_req_pause = false; }
void audio_player_toggle(void) { if (s_state == AP_PLAYING) s_req_pause = true; else if (s_state == AP_PAUSED) s_req_pause = false; else if (s_path[0]) audio_player_play(s_path); }
void audio_player_stop(void) { xSemaphoreTake(s_lock, portMAX_DELAY); if (s_task) wait_task_end(); s_state = AP_STOPPED; xSemaphoreGive(s_lock); if (s_cb) s_cb(AP_STOPPED, s_cb_user); }
ap_state_t audio_player_state(void) { return s_state; }
const char *audio_player_current(void) { return s_path; }
bool audio_player_info(ap_info_t *out) { *out = s_info; return s_state != AP_STOPPED; }
void audio_player_set_volume(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (pct != s_volume) {
        s_volume = pct;
        s_volume_changed_tick = xTaskGetTickCount();
    }
    g_settings.volume = pct;
}
int audio_player_volume(void) { return s_volume; }
void audio_player_set_event_cb(ap_event_cb_t cb, void *user) { s_cb = cb; s_cb_user = user; }
const char *audio_player_last_error(void) { return s_err; }
