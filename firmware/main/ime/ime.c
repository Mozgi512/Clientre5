/* Input method engine: English passthrough, Japanese (romaji -> kana -> SKK
 * dictionary kanji conversion), Chinese (pinyin -> hanzi). */
#include "ime.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "storage/sd.h"

static const char *TAG = "ime";
static const char *ui_basename_local(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }

extern const uint8_t skk_m_bin_start[] asm("_binary_skk_m_bin_start");
extern const uint8_t skk_m_bin_end[]   asm("_binary_skk_m_bin_end");
extern const uint8_t pinyin_bin_start[] asm("_binary_pinyin_bin_start");
extern const uint8_t pinyin_bin_end[]   asm("_binary_pinyin_bin_end");

static ime_dict_t *s_skk_builtin, *s_skk_sd, *s_pinyin;
static char s_dict_info[64] = "built-in";

static ime_commit_cb_t s_commit_cb; static void *s_commit_user;
static ime_update_cb_t s_update_cb; static void *s_update_user;

/* composing state */
static char s_kana[192];      /* converted kana (hiragana) or pinyin letters */
static char s_pending[8];     /* unconsumed romaji */
static char s_preedit[224];
static char s_cands[IME_MAX_CANDS][96];
static int s_ncand = 0, s_cand_idx = -1;
static bool s_converting = false;

static void notify(void) { if (s_update_cb) s_update_cb(s_update_user); }

static void commit(const char *s)
{
    if (s && *s && s_commit_cb) s_commit_cb(s, s_commit_user);
}

void ime_init(void)
{
    s_skk_builtin = ime_dict_open_mem(skk_m_bin_start, skk_m_bin_end - skk_m_bin_start);
    s_pinyin = ime_dict_open_mem(pinyin_bin_start, pinyin_bin_end - pinyin_bin_start);
    ESP_LOGI(TAG, "SKK built-in: %d entries, pinyin: %d entries", ime_dict_count(s_skk_builtin), ime_dict_count(s_pinyin));
    if (g_settings.ime_skk_sd) ime_load_sd_dict(SD_MOUNT "/skk/SKK-JISYO.L");
}

bool ime_load_sd_dict(const char *path)
{
    if (!fs_exists(path)) return false;
    ime_dict_t *d = ime_dict_open_skk_text(path);
    if (!d) return false;
    if (s_skk_sd) ime_dict_close(s_skk_sd);
    s_skk_sd = d;
    snprintf(s_dict_info, sizeof(s_dict_info), "%s (%d)", ui_basename_local(path), ime_dict_count(d));
    return true;
}

const char *ime_dict_info(void) { return s_dict_info; }

ime_src_t ime_get_source(void) { return (ime_src_t)g_settings.ime_src; }
void ime_set_source(ime_src_t src)
{
    ime_commit_preedit();
    g_settings.ime_src = src % IME_SRC_COUNT;
    settings_save();
    notify();
}
void ime_toggle_source(void)
{
    ime_commit_preedit();
    /* EN -> JA -> ZH -> EN, but skip ZH unless UI language is Chinese */
    ime_src_t s = ime_get_source();
    lang_t l = i18n_get_lang();
    if (s == IME_SRC_EN) s = IME_SRC_JA;
    else if (s == IME_SRC_JA) s = (l == LANG_ZH_CN || l == LANG_ZH_TW) ? IME_SRC_ZH : IME_SRC_EN;
    else s = IME_SRC_EN;
    ime_set_source(s);
}
ime_ja_mode_t ime_get_ja_mode(void) { return (ime_ja_mode_t)g_settings.ime_ja_mode; }
void ime_set_ja_mode(ime_ja_mode_t m) { ime_commit_preedit(); g_settings.ime_ja_mode = m; settings_save(); notify(); }
void ime_toggle_ja_mode(void)
{
    if (ime_get_source() != IME_SRC_JA) return;
    ime_set_ja_mode(ime_get_ja_mode() == IME_JA_HIRA ? IME_JA_KATA : IME_JA_HIRA);
}
bool ime_is_active(void) { return ime_get_source() != IME_SRC_EN; }
bool ime_is_composing(void) { return s_kana[0] || s_pending[0]; }

void ime_set_commit_cb(ime_commit_cb_t cb, void *user) { s_commit_cb = cb; s_commit_user = user; }
void ime_set_update_cb(ime_update_cb_t cb, void *user) { s_update_cb = cb; s_update_user = user; }

const char *ime_status_label(void)
{
    switch (ime_get_source()) {
    case IME_SRC_JA: return ime_get_ja_mode() == IME_JA_KATA ? "ア" : "あ";
    case IME_SRC_ZH: return "拼";
    default: return "A";
    }
}

static void clear_state(void)
{
    s_kana[0] = 0; s_pending[0] = 0; s_preedit[0] = 0;
    s_ncand = 0; s_cand_idx = -1; s_converting = false;
}

void ime_reset(void) { clear_state(); notify(); }

const char *ime_preedit(void)
{
    if (s_converting && s_cand_idx >= 0 && s_cand_idx < s_ncand) return s_cands[s_cand_idx];
    if (ime_get_source() == IME_SRC_JA && ime_get_ja_mode() == IME_JA_KATA) {
        char k[192]; kana_to_katakana(s_kana, k, sizeof(k));
        snprintf(s_preedit, sizeof(s_preedit), "%s%s", k, s_pending);
    } else {
        snprintf(s_preedit, sizeof(s_preedit), "%s%s", s_kana, s_pending);
    }
    return s_preedit;
}

int ime_candidate_count(void) { return s_converting ? s_ncand : 0; }
const char *ime_candidate(int i) { return (i >= 0 && i < s_ncand) ? s_cands[i] : ""; }
int ime_candidate_index(void) { return s_cand_idx; }

static void add_cand(const char *s)
{
    if (!s || !*s || s_ncand >= IME_MAX_CANDS) return;
    for (int i = 0; i < s_ncand; i++) if (!strcmp(s_cands[i], s)) return;
    strlcpy(s_cands[s_ncand++], s, sizeof(s_cands[0]));
}

/* number of bytes of the last UTF-8 character */
static size_t last_char_len(const char *s)
{
    size_t n = strlen(s);
    if (!n) return 0;
    size_t i = n - 1;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    return n - i;
}
static size_t char_len_at(const char *s)
{
    unsigned char c = *s;
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    return 4;
}
static int utf8_len(const char *s) { int n = 0; while (*s) { s += char_len_at(s); n++; } return n; }

/* Okurigana: the first romaji consonant of a kana (for SKK "okuri-ari" keys). */
static char okuri_letter(const char *kana)
{
    static const struct { const char *k; char c; } t[] = {
        {"か",'k'},{"き",'k'},{"く",'k'},{"け",'k'},{"こ",'k'},{"が",'g'},{"ぎ",'g'},{"ぐ",'g'},{"げ",'g'},{"ご",'g'},
        {"さ",'s'},{"し",'s'},{"す",'s'},{"せ",'s'},{"そ",'s'},{"ざ",'z'},{"じ",'z'},{"ず",'z'},{"ぜ",'z'},{"ぞ",'z'},
        {"た",'t'},{"ち",'t'},{"つ",'t'},{"て",'t'},{"と",'t'},{"だ",'d'},{"ぢ",'d'},{"づ",'d'},{"で",'d'},{"ど",'d'},
        {"な",'n'},{"に",'n'},{"ぬ",'n'},{"ね",'n'},{"の",'n'},{"は",'h'},{"ひ",'h'},{"ふ",'h'},{"へ",'h'},{"ほ",'h'},
        {"ば",'b'},{"び",'b'},{"ぶ",'b'},{"べ",'b'},{"ぼ",'b'},{"ぱ",'p'},{"ぴ",'p'},{"ぷ",'p'},{"ぺ",'p'},{"ぽ",'p'},
        {"ま",'m'},{"み",'m'},{"む",'m'},{"め",'m'},{"も",'m'},{"や",'y'},{"ゆ",'y'},{"よ",'y'},
        {"ら",'r'},{"り",'r'},{"る",'r'},{"れ",'r'},{"ろ",'r'},{"わ",'w'},{"を",'w'},{"ん",'n'},
        {"あ",'a'},{"い",'i'},{"う",'u'},{"え",'e'},{"お",'o'},{"っ",'t'},
    };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) if (!strncmp(kana, t[i].k, 3)) return t[i].c;
    return 0;
}

static void lookup_all(const char *key, const char *suffix)
{
    const char *res[48];
    ime_dict_t *dicts[2] = { s_skk_sd, s_skk_builtin };
    for (int d = 0; d < 2; d++) {
        if (!dicts[d]) continue;
        int n = ime_dict_lookup(dicts[d], key, res, 48);
        for (int i = 0; i < n; i++) {
            char tmp[96];
            snprintf(tmp, sizeof(tmp), "%s%s", res[i], suffix ? suffix : "");
            add_cand(tmp);
        }
    }
}

static void build_ja_candidates(void)
{
    s_ncand = 0;
    const char *r = s_kana;
    size_t rl = strlen(r);
    if (!rl) return;
    /* 1. exact reading */
    lookup_all(r, "");
    /* 2. okurigana: prefix + consonant of the remaining kana, suffix appended */
    int nchars = utf8_len(r);
    if (nchars >= 2) {
        /* try splitting last 1..2 chars as okurigana */
        for (int cut = 1; cut <= 2 && cut < nchars; cut++) {
            const char *p = r; for (int i = 0; i < nchars - cut; i++) p += char_len_at(p);
            char prefix[192]; size_t pl = p - r; memcpy(prefix, r, pl);
            char ok = okuri_letter(p);
            if (!ok) continue;
            prefix[pl] = ok; prefix[pl + 1] = 0;
            lookup_all(prefix, p);
        }
    }
    /* 3. longest dictionary prefix + rest as kana (simple segmentation) */
    if (nchars >= 2) {
        for (int keep = nchars - 1; keep >= 1 && s_ncand < 12; keep--) {
            const char *p = r; for (int i = 0; i < keep; i++) p += char_len_at(p);
            char prefix[192]; size_t pl = p - r; memcpy(prefix, r, pl); prefix[pl] = 0;
            lookup_all(prefix, p);
        }
    }
    /* 4. kana forms */
    char kata[192]; kana_to_katakana(r, kata, sizeof(kata));
    if (ime_get_ja_mode() == IME_JA_KATA) { add_cand(kata); add_cand(r); }
    else { add_cand(r); add_cand(kata); }
    /* 5. half-width katakana */
    char hankaku[400]; kana_to_hankaku(r, hankaku, sizeof(hankaku));
    if (strcmp(hankaku, kata)) add_cand(hankaku);
}

static void build_zh_candidates(void)
{
    s_ncand = 0;
    if (!s_kana[0]) return;
    const char *res[48];
    int n = s_pinyin ? ime_dict_lookup(s_pinyin, s_kana, res, 48) : 0;
    for (int i = 0; i < n; i++) add_cand(res[i]);
    /* prefix segmentation: longest pinyin syllable prefix + rest */
    size_t l = strlen(s_kana);
    for (size_t keep = l - 1; keep >= 1 && s_ncand < 20; keep--) {
        char pre[32]; if (keep >= sizeof(pre)) continue;
        memcpy(pre, s_kana, keep); pre[keep] = 0;
        n = s_pinyin ? ime_dict_lookup(s_pinyin, pre, res, 24) : 0;
        for (int i = 0; i < n && s_ncand < IME_MAX_CANDS; i++) {
            char tmp[96]; snprintf(tmp, sizeof(tmp), "%s%s", res[i], s_kana + keep);
            add_cand(tmp);
        }
        if (n) break;
    }
    add_cand(s_kana);
}

static void start_convert(void)
{
    romaji_flush(s_pending, s_kana, sizeof(s_kana));
    if (!s_kana[0]) return;
    if (ime_get_source() == IME_SRC_JA) build_ja_candidates(); else build_zh_candidates();
    if (s_ncand == 0) add_cand(s_kana);
    s_converting = true;
    s_cand_idx = 0;
}

void ime_select_candidate(int i)
{
    if (i < 0 || i >= s_ncand) return;
    char out[96]; strlcpy(out, s_cands[i], sizeof(out));
    clear_state();
    commit(out);
    notify();
}

void ime_commit_preedit(void)
{
    if (!ime_is_composing()) return;
    if (s_converting && s_cand_idx >= 0) { ime_select_candidate(s_cand_idx); return; }
    romaji_flush(s_pending, s_kana, sizeof(s_kana));
    char out[224];
    if (ime_get_source() == IME_SRC_JA && ime_get_ja_mode() == IME_JA_KATA) kana_to_katakana(s_kana, out, sizeof(out));
    else strlcpy(out, s_kana, sizeof(out));
    clear_state();
    commit(out);
    notify();
}

static void backspace(void)
{
    if (s_converting) { s_converting = false; s_ncand = 0; s_cand_idx = -1; return; }
    if (s_pending[0]) { s_pending[strlen(s_pending) - 1] = 0; return; }
    size_t l = last_char_len(s_kana);
    s_kana[strlen(s_kana) - l] = 0;
}

bool ime_feed_key(const key_event_t *ev)
{
    ime_src_t src = ime_get_source();
    if (src == IME_SRC_EN) return false;
    bool composing = ime_is_composing();

    if (ev->type == KEY_EV_CHAR) {
        if (ev->mods & (MOD_CTRL | MOD_ALT | MOD_META)) { ime_commit_preedit(); return false; }
        uint32_t c = ev->cp;
        if (c == ' ') {
            if (!composing) return false;                 /* plain space passes through */
            if (!s_converting) start_convert();
            else if (s_ncand > 0) s_cand_idx = (s_cand_idx + 1) % s_ncand;
            notify();
            return true;
        }
        if (s_converting) {
            if (c >= '1' && c <= '9') {
                int page = (s_cand_idx / IME_PAGE) * IME_PAGE;
                int idx = page + (c - '1');
                if (idx < s_ncand) { ime_select_candidate(idx); return true; }
            }
            /* any other character commits the current candidate, then continues */
            ime_select_candidate(s_cand_idx);
            composing = false;
        }
        if (src == IME_SRC_JA) {
            if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
            if ((c >= 'a' && c <= 'z') || c == '\'' || c == '-' ) {
                romaji_feed(s_pending, sizeof(s_pending), s_kana, sizeof(s_kana), (char)c);
                notify(); return true;
            }
            if (c == ',' || c == '.' || c == '[' || c == ']' || c == '/' || c == '!' || c == '?' || c == '~') {
                /* punctuation: convert to Japanese punctuation, committing preedit first */
                ime_commit_preedit();
                char p[8]; strlcpy(p, "", sizeof(p));
                switch (c) { case ',': strcpy(p, "、"); break; case '.': strcpy(p, "。"); break; case '[': strcpy(p, "「"); break;
                             case ']': strcpy(p, "」"); break; case '/': strcpy(p, "・"); break; case '!': strcpy(p, "！"); break;
                             case '?': strcpy(p, "？"); break; case '~': strcpy(p, "〜"); break; }
                commit(p); notify(); return true;
            }
            /* digits & other symbols pass through after committing */
            ime_commit_preedit();
            return false;
        } else { /* pinyin */
            if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
            if (c >= 'a' && c <= 'z') {
                size_t l = strlen(s_kana);
                if (l + 1 < sizeof(s_kana)) { s_kana[l] = (char)c; s_kana[l + 1] = 0; }
                notify(); return true;
            }
            if (c == '\'' && composing) return true;   /* syllable separator, ignored */
            ime_commit_preedit();
            if (c == ',') { commit("，"); return true; }
            if (c == '.') { commit("。"); return true; }
            if (c == '?') { commit("？"); return true; }
            if (c == '!') { commit("！"); return true; }
            return false;
        }
    }

    /* special keys */
    if (!composing) return false;
    switch (ev->key) {
    case SK_ENTER:
        ime_commit_preedit();
        return true;
    case SK_ESC:
        if (s_converting) { s_converting = false; s_ncand = 0; s_cand_idx = -1; }
        else clear_state();
        notify(); return true;
    case SK_BACKSPACE:
        backspace(); notify(); return true;
    case SK_DOWN: case SK_RIGHT:
        if (!s_converting) start_convert();
        else if (s_ncand) s_cand_idx = (s_cand_idx + 1) % s_ncand;
        notify(); return true;
    case SK_UP: case SK_LEFT:
        if (s_converting && s_ncand) s_cand_idx = (s_cand_idx + s_ncand - 1) % s_ncand;
        notify(); return true;
    case SK_PGDN:
        if (s_converting && s_ncand) { s_cand_idx += IME_PAGE; if (s_cand_idx >= s_ncand) s_cand_idx = 0; }
        notify(); return true;
    case SK_PGUP:
        if (s_converting && s_ncand) { s_cand_idx -= IME_PAGE; if (s_cand_idx < 0) s_cand_idx = 0; }
        notify(); return true;
    case SK_TAB:
        ime_commit_preedit(); return true;
    case SK_F6: { /* hiragana */
        romaji_flush(s_pending, s_kana, sizeof(s_kana));
        char out[192]; strlcpy(out, s_kana, sizeof(out)); clear_state(); commit(out); notify(); return true; }
    case SK_F7: { /* katakana */
        romaji_flush(s_pending, s_kana, sizeof(s_kana));
        char out[192]; kana_to_katakana(s_kana, out, sizeof(out)); clear_state(); commit(out); notify(); return true; }
    case SK_F8: { /* half-width katakana */
        romaji_flush(s_pending, s_kana, sizeof(s_kana));
        char out[400]; kana_to_hankaku(s_kana, out, sizeof(out)); clear_state(); commit(out); notify(); return true; }
    case SK_F9: case SK_F10: { /* zenkaku / hankaku romaji of pending+kana: use romaji if any */
        char out[192]; if (ev->key == SK_F9) ascii_to_zenkaku(s_pending, out, sizeof(out)); else strlcpy(out, s_pending, sizeof(out));
        if (!out[0]) return true;
        clear_state(); commit(out); notify(); return true; }
    default:
        return true;   /* swallow other keys while composing */
    }
}
