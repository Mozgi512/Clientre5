#include "ime.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

typedef struct { const char *r; const char *k; } rk_t;
/* Longest keys first is not required; we match by exact table lookup. */
static const rk_t s_tbl[] = {
    {"a","あ"},{"i","い"},{"u","う"},{"e","え"},{"o","お"},
    {"ka","か"},{"ki","き"},{"ku","く"},{"ke","け"},{"ko","こ"},
    {"sa","さ"},{"si","し"},{"shi","し"},{"su","す"},{"se","せ"},{"so","そ"},
    {"ta","た"},{"ti","ち"},{"chi","ち"},{"tu","つ"},{"tsu","つ"},{"te","て"},{"to","と"},
    {"na","な"},{"ni","に"},{"nu","ぬ"},{"ne","ね"},{"no","の"},
    {"ha","は"},{"hi","ひ"},{"hu","ふ"},{"fu","ふ"},{"he","へ"},{"ho","ほ"},
    {"ma","ま"},{"mi","み"},{"mu","む"},{"me","め"},{"mo","も"},
    {"ya","や"},{"yi","い"},{"yu","ゆ"},{"ye","いぇ"},{"yo","よ"},
    {"ra","ら"},{"ri","り"},{"ru","る"},{"re","れ"},{"ro","ろ"},
    {"wa","わ"},{"wi","うぃ"},{"wu","う"},{"we","うぇ"},{"wo","を"},
    {"nn","ん"},{"n'","ん"},{"xn","ん"},
    {"ga","が"},{"gi","ぎ"},{"gu","ぐ"},{"ge","げ"},{"go","ご"},
    {"za","ざ"},{"zi","じ"},{"ji","じ"},{"zu","ず"},{"ze","ぜ"},{"zo","ぞ"},
    {"da","だ"},{"di","ぢ"},{"du","づ"},{"de","で"},{"do","ど"},
    {"ba","ば"},{"bi","び"},{"bu","ぶ"},{"be","べ"},{"bo","ぼ"},
    {"pa","ぱ"},{"pi","ぴ"},{"pu","ぷ"},{"pe","ぺ"},{"po","ぽ"},
    {"kya","きゃ"},{"kyi","きぃ"},{"kyu","きゅ"},{"kye","きぇ"},{"kyo","きょ"},
    {"sya","しゃ"},{"syu","しゅ"},{"syo","しょ"},{"sha","しゃ"},{"shu","しゅ"},{"she","しぇ"},{"sho","しょ"},{"syi","しぃ"},{"sye","しぇ"},
    {"tya","ちゃ"},{"tyu","ちゅ"},{"tyo","ちょ"},{"cha","ちゃ"},{"chu","ちゅ"},{"che","ちぇ"},{"cho","ちょ"},{"tyi","ちぃ"},{"tye","ちぇ"},{"cya","ちゃ"},{"cyu","ちゅ"},{"cyo","ちょ"},
    {"nya","にゃ"},{"nyi","にぃ"},{"nyu","にゅ"},{"nye","にぇ"},{"nyo","にょ"},
    {"hya","ひゃ"},{"hyi","ひぃ"},{"hyu","ひゅ"},{"hye","ひぇ"},{"hyo","ひょ"},
    {"mya","みゃ"},{"myi","みぃ"},{"myu","みゅ"},{"mye","みぇ"},{"myo","みょ"},
    {"rya","りゃ"},{"ryi","りぃ"},{"ryu","りゅ"},{"rye","りぇ"},{"ryo","りょ"},
    {"gya","ぎゃ"},{"gyi","ぎぃ"},{"gyu","ぎゅ"},{"gye","ぎぇ"},{"gyo","ぎょ"},
    {"zya","じゃ"},{"zyu","じゅ"},{"zyo","じょ"},{"ja","じゃ"},{"ju","じゅ"},{"je","じぇ"},{"jo","じょ"},{"jya","じゃ"},{"jyu","じゅ"},{"jyo","じょ"},{"zyi","じぃ"},{"zye","じぇ"},{"jyi","じぃ"},{"jye","じぇ"},
    {"dya","ぢゃ"},{"dyu","ぢゅ"},{"dyo","ぢょ"},{"dyi","ぢぃ"},{"dye","ぢぇ"},
    {"bya","びゃ"},{"byi","びぃ"},{"byu","びゅ"},{"bye","びぇ"},{"byo","びょ"},
    {"pya","ぴゃ"},{"pyi","ぴぃ"},{"pyu","ぴゅ"},{"pye","ぴぇ"},{"pyo","ぴょ"},
    {"fa","ふぁ"},{"fi","ふぃ"},{"fe","ふぇ"},{"fo","ふぉ"},{"fya","ふゃ"},{"fyu","ふゅ"},{"fyo","ふょ"},
    {"va","ゔぁ"},{"vi","ゔぃ"},{"vu","ゔ"},{"ve","ゔぇ"},{"vo","ゔぉ"},
    {"tsa","つぁ"},{"tsi","つぃ"},{"tse","つぇ"},{"tso","つぉ"},
    {"tha","てゃ"},{"thi","てぃ"},{"thu","てゅ"},{"the","てぇ"},{"tho","てょ"},
    {"dha","でゃ"},{"dhi","でぃ"},{"dhu","でゅ"},{"dhe","でぇ"},{"dho","でょ"},
    {"twu","とぅ"},{"dwu","どぅ"},{"kwa","くぁ"},{"gwa","ぐぁ"},{"qa","くぁ"},{"qi","くぃ"},{"qe","くぇ"},{"qo","くぉ"},
    {"xa","ぁ"},{"xi","ぃ"},{"xu","ぅ"},{"xe","ぇ"},{"xo","ぉ"},
    {"la","ぁ"},{"li","ぃ"},{"lu","ぅ"},{"le","ぇ"},{"lo","ぉ"},
    {"xya","ゃ"},{"xyu","ゅ"},{"xyo","ょ"},{"lya","ゃ"},{"lyu","ゅ"},{"lyo","ょ"},
    {"xtu","っ"},{"xtsu","っ"},{"ltu","っ"},{"ltsu","っ"},{"xwa","ゎ"},{"lwa","ゎ"},{"xka","ゕ"},{"xke","ゖ"},
    {"-","ー"},{",","、"},{".","。"},{"[","「"},{"]","」"},{"/","・"},{"!","！"},{"?","？"},{"~","〜"},
};

static bool is_vowel(char c) { return c == 'a' || c == 'i' || c == 'u' || c == 'e' || c == 'o'; }

static const char *lookup_exact(const char *s)
{
    for (size_t i = 0; i < sizeof(s_tbl) / sizeof(s_tbl[0]); i++) if (!strcmp(s_tbl[i].r, s)) return s_tbl[i].k;
    return NULL;
}
static bool is_prefix(const char *s)
{
    size_t n = strlen(s);
    for (size_t i = 0; i < sizeof(s_tbl) / sizeof(s_tbl[0]); i++) if (!strncmp(s_tbl[i].r, s, n) && strlen(s_tbl[i].r) > n) return true;
    return false;
}
static void append(char *dst, size_t cap, const char *s)
{
    size_t l = strlen(dst);
    if (l + strlen(s) + 1 <= cap) strcpy(dst + l, s);
}

bool romaji_feed(char *pending, size_t pend_cap, char *kana, size_t kana_cap, char c)
{
    size_t pl = strlen(pending);
    if (pl + 2 > pend_cap) { pending[0] = 0; pl = 0; }
    /* "n" followed by a consonant (other than y / n / ') -> ん */
    if (pl == 1 && pending[0] == 'n' && !is_vowel(c) && c != 'y' && c != 'n' && c != '\'') {
        append(kana, kana_cap, "ん");
        pending[0] = 0; pl = 0;
    }
    /* doubled consonant -> っ  (kk, ss, tt, pp, ...; not nn, not vowels) */
    if (pl >= 1 && pending[pl - 1] == c && !is_vowel(c) && c != 'n' && ((c >= 'a' && c <= 'z'))) {
        append(kana, kana_cap, "っ");
        pending[pl - 1] = 0; pl--;
    }
    pending[pl++] = c; pending[pl] = 0;
    const char *k = lookup_exact(pending);
    if (k) { append(kana, kana_cap, k); pending[0] = 0; return true; }
    if (is_prefix(pending)) return true;
    /* no match: drop leading chars until something matches or is a prefix */
    while (pending[0]) {
        memmove(pending, pending + 1, strlen(pending));
        if (!pending[0]) break;
        k = lookup_exact(pending);
        if (k) { append(kana, kana_cap, k); pending[0] = 0; return true; }
        if (is_prefix(pending)) return true;
    }
    return true;
}

void romaji_flush(char *pending, char *kana, size_t kana_cap)
{
    if (!strcmp(pending, "n")) append(kana, kana_cap, "ん");
    else if (pending[0]) append(kana, kana_cap, pending);   /* leave stray romaji as-is */
    pending[0] = 0;
}

static int utf8_decode(const char *s, uint32_t *cp)
{
    unsigned char c = s[0];
    if (c < 0x80) { *cp = c; return 1; }
    if ((c & 0xE0) == 0xC0) { *cp = ((c & 0x1F) << 6) | (s[1] & 0x3F); return 2; }
    if ((c & 0xF0) == 0xE0) { *cp = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); return 3; }
    *cp = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); return 4;
}
static int utf8_encode(uint32_t c, char *b)
{
    int n = 0;
    if (c < 0x80) b[n++] = c;
    else if (c < 0x800) { b[n++] = 0xC0 | (c >> 6); b[n++] = 0x80 | (c & 0x3F); }
    else if (c < 0x10000) { b[n++] = 0xE0 | (c >> 12); b[n++] = 0x80 | ((c >> 6) & 0x3F); b[n++] = 0x80 | (c & 0x3F); }
    else { b[n++] = 0xF0 | (c >> 18); b[n++] = 0x80 | ((c >> 12) & 0x3F); b[n++] = 0x80 | ((c >> 6) & 0x3F); b[n++] = 0x80 | (c & 0x3F); }
    return n;
}

void kana_to_katakana(const char *hira, char *out, size_t cap)
{
    size_t o = 0;
    while (*hira && o + 5 < cap) {
        uint32_t cp; int n = utf8_decode(hira, &cp); hira += n;
        if (cp >= 0x3041 && cp <= 0x3096) cp += 0x60;
        o += utf8_encode(cp, out + o);
    }
    out[o] = 0;
}

/* Half-width katakana table for U+30A1..U+30FF; two codepoints for the glyphs
 * that decompose into a base letter plus a voiced/semi-voiced mark. */
static const uint16_t s_hankaku[0x5F][2] = {
    {0xFF67,0},{0xFF71,0},{0xFF68,0},{0xFF72,0},{0xFF69,0},{0xFF73,0},{0xFF6A,0},{0xFF74,0},
    {0xFF6B,0},{0xFF75,0},{0xFF76,0},{0xFF76,0xFF9E},{0xFF77,0},{0xFF77,0xFF9E},{0xFF78,0},{0xFF78,0xFF9E},
    {0xFF79,0},{0xFF79,0xFF9E},{0xFF7A,0},{0xFF7A,0xFF9E},{0xFF7B,0},{0xFF7B,0xFF9E},{0xFF7C,0},{0xFF7C,0xFF9E},
    {0xFF7D,0},{0xFF7D,0xFF9E},{0xFF7E,0},{0xFF7E,0xFF9E},{0xFF7F,0},{0xFF7F,0xFF9E},{0xFF80,0},{0xFF80,0xFF9E},
    {0xFF81,0},{0xFF81,0xFF9E},{0xFF6F,0},{0xFF82,0},{0xFF82,0xFF9E},{0xFF83,0},{0xFF83,0xFF9E},{0xFF84,0},
    {0xFF84,0xFF9E},{0xFF85,0},{0xFF86,0},{0xFF87,0},{0xFF88,0},{0xFF89,0},{0xFF8A,0},{0xFF8A,0xFF9E},
    {0xFF8A,0xFF9F},{0xFF8B,0},{0xFF8B,0xFF9E},{0xFF8B,0xFF9F},{0xFF8C,0},{0xFF8C,0xFF9E},{0xFF8C,0xFF9F},{0xFF8D,0},
    {0xFF8D,0xFF9E},{0xFF8D,0xFF9F},{0xFF8E,0},{0xFF8E,0xFF9E},{0xFF8E,0xFF9F},{0xFF8F,0},{0xFF90,0},{0xFF91,0},
    {0xFF92,0},{0xFF93,0},{0xFF6C,0},{0xFF94,0},{0xFF6D,0},{0xFF95,0},{0xFF6E,0},{0xFF96,0},
    {0xFF97,0},{0xFF98,0},{0xFF99,0},{0xFF9A,0},{0xFF9B,0},{0,0},{0xFF9C,0},{0,0},
    {0,0},{0xFF66,0},{0xFF9D,0},{0xFF73,0xFF9E},{0,0},{0,0},{0xFF9C,0xFF9E},{0,0},
    {0,0},{0xFF66,0xFF9E},{0xFF65,0},{0xFF70,0},{0,0},{0,0},{0,0},
};

void kana_to_hankaku(const char *kana, char *out, size_t cap)
{
    size_t o = 0;
    while (*kana && o + 7 < cap) {
        uint32_t cp; int n = utf8_decode(kana, &cp); kana += n;
        if (cp >= 0x3041 && cp <= 0x3096) cp += 0x60;          /* hiragana first */
        if (cp >= 0x30A1 && cp <= 0x30FF) {
            const uint16_t *h = s_hankaku[cp - 0x30A1];
            if (h[0]) {
                o += utf8_encode(h[0], out + o);
                if (h[1]) o += utf8_encode(h[1], out + o);
                continue;
            }
        } else if (cp == 0x3002) cp = 0xFF61;                  /* 。 */
        else if (cp == 0x300C) cp = 0xFF62;                    /* 「 */
        else if (cp == 0x300D) cp = 0xFF63;                    /* 」 */
        else if (cp == 0x3001) cp = 0xFF64;                    /* 、 */
        else if (cp == 0x309B) cp = 0xFF9E;                    /* ゛ */
        else if (cp == 0x309C) cp = 0xFF9F;                    /* ゜ */
        else if (cp == 0x3000) cp = ' ';
        else if (cp >= 0xFF01 && cp <= 0xFF5E) cp -= 0xFEE0;   /* fullwidth ASCII */
        o += utf8_encode(cp, out + o);
    }
    out[o] = 0;
}

void ascii_to_zenkaku(const char *ascii, char *out, size_t cap)
{
    size_t o = 0;
    while (*ascii && o + 5 < cap) {
        uint32_t cp = (unsigned char)*ascii++;
        if (cp == ' ') cp = 0x3000;
        else if (cp > 0x20 && cp < 0x7F) cp = cp - 0x21 + 0xFF01;
        o += utf8_encode(cp, out + o);
    }
    out[o] = 0;
}
