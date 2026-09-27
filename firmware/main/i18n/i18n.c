#include "i18n.h"

#define X(id, en, cn, tw, ja) en,
static const char *const tbl_en[STR_COUNT] = { STR_TABLE(X) };
#undef X
#define X(id, en, cn, tw, ja) cn,
static const char *const tbl_cn[STR_COUNT] = { STR_TABLE(X) };
#undef X
#define X(id, en, cn, tw, ja) tw,
static const char *const tbl_tw[STR_COUNT] = { STR_TABLE(X) };
#undef X
#define X(id, en, cn, tw, ja) ja,
static const char *const tbl_ja[STR_COUNT] = { STR_TABLE(X) };
#undef X

static const char *const *const tables[LANG_COUNT] = { tbl_en, tbl_cn, tbl_tw, tbl_ja };
static lang_t s_lang = LANG_EN;

void i18n_set_lang(lang_t lang) { s_lang = (lang < LANG_COUNT) ? lang : LANG_EN; }
lang_t i18n_get_lang(void) { return s_lang; }

const char *i18n_lang_name(lang_t lang)
{
    switch (lang) {
    case LANG_EN: return "English";
    case LANG_ZH_CN: return "简体中文";
    case LANG_ZH_TW: return "繁體中文";
    case LANG_JA: return "日本語";
    default: return "?";
    }
}

const char *tr(str_id_t id) { return tr_lang(s_lang, id); }

const char *tr_lang(lang_t lang, str_id_t id)
{
    if (id >= STR_COUNT) return "";
    if (lang >= LANG_COUNT) lang = LANG_EN;
    const char *s = tables[lang][id];
    return (s && *s) ? s : tbl_en[id];
}
