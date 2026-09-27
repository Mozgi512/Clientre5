#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { AP_STOPPED = 0, AP_PLAYING, AP_PAUSED, AP_ERROR } ap_state_t;
typedef struct {
    char title[96], artist[96], album[96];
    uint32_t duration_ms, position_ms;
    int sample_rate, channels, bitrate;
} ap_info_t;
typedef void (*ap_event_cb_t)(ap_state_t st, void *user);
void audio_player_init(void);
bool audio_player_play(const char *path);
void audio_player_pause(void);
void audio_player_resume(void);
void audio_player_stop(void);
void audio_player_toggle(void);
ap_state_t audio_player_state(void);
const char *audio_player_current(void);
bool audio_player_info(ap_info_t *out);
void audio_player_set_volume(int pct);
int  audio_player_volume(void);
void audio_player_set_event_cb(ap_event_cb_t cb, void *user);
/* Cover art (JPEG/PNG bytes) from ID3v2 APIC or FLAC PICTURE. Caller frees. */
bool audio_player_cover(const char *path, uint8_t **data, size_t *len, char *mime, size_t mime_cap);
const char *audio_player_last_error(void);
#ifdef __cplusplus
}
#endif
