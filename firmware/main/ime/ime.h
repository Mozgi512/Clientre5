#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "settings/settings.h"
#include "input/keyboard.h"
#ifdef __cplusplus
extern "C" {
#endif

#define IME_MAX_CANDS 64
#define IME_PAGE 9

typedef void (*ime_commit_cb_t)(const char *utf8, void *user);
typedef void (*ime_update_cb_t)(void *user);

void ime_init(void);
ime_src_t ime_get_source(void);
void ime_set_source(ime_src_t src);
void ime_toggle_source(void);
ime_ja_mode_t ime_get_ja_mode(void);
void ime_set_ja_mode(ime_ja_mode_t m);
void ime_toggle_ja_mode(void);
bool ime_is_active(void);            /* source != EN */
bool ime_is_composing(void);
bool ime_feed_key(const key_event_t *ev);   /* true if consumed */
void ime_reset(void);                       /* drop preedit */
void ime_commit_preedit(void);              /* commit as-is */
const char *ime_preedit(void);              /* kana + pending romaji, UTF-8 */
int ime_candidate_count(void);
const char *ime_candidate(int i);
int ime_candidate_index(void);
void ime_select_candidate(int i);
void ime_set_commit_cb(ime_commit_cb_t cb, void *user);
void ime_set_update_cb(ime_update_cb_t cb, void *user);
const char *ime_status_label(void);        /* "A" / "あ" / "ア" / "拼" */
bool ime_load_sd_dict(const char *path);   /* SKK-JISYO (EUC-JP or UTF-8) into PSRAM */
const char *ime_dict_info(void);

/* romaji -> kana helpers (ime_romaji.c) */
/* Appends to `kana` (UTF-8, cap bytes); `pending` holds unconsumed romaji.
 * Returns true if anything changed. */
bool romaji_feed(char *pending, size_t pend_cap, char *kana, size_t kana_cap, char c);
void romaji_flush(char *pending, char *kana, size_t kana_cap);   /* commit lone 'n' etc. */
void kana_to_katakana(const char *hira, char *out, size_t cap);
void kana_to_hankaku(const char *kana, char *out, size_t cap);
void ascii_to_zenkaku(const char *ascii, char *out, size_t cap);

/* dictionary (ime_dict.c) */
typedef struct ime_dict ime_dict_t;
ime_dict_t *ime_dict_open_mem(const uint8_t *data, size_t len);   /* IMD1 binary */
ime_dict_t *ime_dict_open_skk_text(const char *path);            /* SKK-JISYO text */
void ime_dict_close(ime_dict_t *d);
/* Fills `out` with up to max UTF-8 candidates (pointers valid until next call). */
int ime_dict_lookup(ime_dict_t *d, const char *reading, const char **out, int max);
int ime_dict_count(ime_dict_t *d);

#ifdef __cplusplus
}
#endif
