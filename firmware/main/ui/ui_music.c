/* Music player screen, favorites, and the background playback bar. */
#include "ui.h"
#include "media/audio_player.h"
#include "media/image_loader.h"
#include "storage/sd.h"
#include "settings/settings.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

#define MAX_TRACKS 400
typedef struct { char path[300]; } track_t;
static track_t *s_tracks = NULL; static int s_ntracks = 0; static int s_cur = -1;
static bool s_show_fav = false;
static char (*s_fav)[300] = NULL; static int s_nfav = 0;

static lv_obj_t *s_scr, *s_title, *s_sub, *s_time, *s_cover, *s_play_btn, *s_list, *s_slider;
static lv_image_dsc_t *s_cover_dsc = NULL;

/* ---- favorites (NVS blob of paths) ---- */
static void fav_load(void)
{
    void *b = NULL; size_t l = 0; free(s_fav); s_fav = NULL; s_nfav = 0;
    if (nvs_get_blob_alloc("music", "fav", &b, &l)) { s_nfav = l / 300; s_fav = b; }
}
static void fav_save(void) { if (s_nfav) nvs_set_blob_ns("music", "fav", s_fav, s_nfav * 300); else nvs_erase_key_ns("music", "fav"); }
static int fav_find(const char *p) { for (int i = 0; i < s_nfav; i++) if (!strcmp(s_fav[i], p)) return i; return -1; }
static void fav_toggle(const char *p)
{
    int i = fav_find(p);
    if (i >= 0) { memmove(&s_fav[i], &s_fav[i + 1], (s_nfav - i - 1) * 300); s_nfav--; ui_toast(tr(STR_REMOVED_FAV)); }
    else { s_fav = realloc(s_fav, (s_nfav + 1) * 300); strlcpy(s_fav[s_nfav++], p, 300); ui_toast(tr(STR_ADDED_FAV)); }
    fav_save();
}

/* ---- scanning ---- */
static bool is_audio(const char *n) { const char *x = fs_ext(n); return !strcmp(x, "mp3") || !strcmp(x, "flac") || !strcmp(x, "wav"); }
static void scan_dir(const char *dir, int depth)
{
    if (depth > 4 || s_ntracks >= MAX_TRACKS) return;
    DIR *d = opendir(dir); if (!d) return;
    struct dirent *de; char full[600];
    while ((de = readdir(d)) && s_ntracks < MAX_TRACKS) {
        if (de->d_name[0] == '.') continue;
        snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
        if (de->d_type == DT_DIR) { if (strcmp(de->d_name, "System Volume Information")) scan_dir(full, depth + 1); }
        else if (is_audio(de->d_name)) strlcpy(s_tracks[s_ntracks++].path, full, 300);
    }
    closedir(d);
}
static int cmp_track(const void *a, const void *b) { return strcasecmp(((const track_t *)a)->path, ((const track_t *)b)->path); }
static void build_list(void)
{
    if (!s_tracks) s_tracks = calloc(MAX_TRACKS, sizeof(track_t));
    s_ntracks = 0;
    if (s_show_fav) { for (int i = 0; i < s_nfav && s_ntracks < MAX_TRACKS; i++) strlcpy(s_tracks[s_ntracks++].path, s_fav[i], 300); }
    else { scan_dir(SD_MOUNT "/Music", 0); if (!s_ntracks) scan_dir(SD_MOUNT, 0); qsort(s_tracks, s_ntracks, sizeof(track_t), cmp_track); }
    s_cur = -1;
    const char *cur = audio_player_current();
    for (int i = 0; i < s_ntracks; i++) if (!strcmp(s_tracks[i].path, cur)) s_cur = i;
}

/* ---- UI ---- */
static void set_cover(const char *path)
{
    if (!s_scr) return;
    lv_image_set_src(s_cover, NULL);
    if (s_cover_dsc) { image_loader_free(s_cover_dsc); s_cover_dsc = NULL; }
    uint8_t *pic = NULL; size_t len = 0; char mime[32];
    if (path && audio_player_cover(path, &pic, &len, mime, sizeof(mime))) {
        if (len > 3 && pic[0] == 0xFF && pic[1] == 0xD8) s_cover_dsc = image_loader_jpeg_mem(pic, len, 260, 260);
        free(pic);
    }
    if (s_cover_dsc) { lv_image_set_src(s_cover, s_cover_dsc); lv_image_set_inner_align(s_cover, LV_IMAGE_ALIGN_CONTAIN); }
    else lv_image_set_src(s_cover, LV_SYMBOL_AUDIO);
}

static void refresh(void)
{
    if (!s_scr) return;
    ap_info_t inf; bool active = audio_player_info(&inf);
    ap_state_t st = audio_player_state();
    const char *cur = audio_player_current();
    if (active || cur[0]) {
        lv_label_set_text(s_title, inf.title[0] ? inf.title : ui_basename(cur));
        char sub[200]; snprintf(sub, sizeof(sub), "%s%s%s", inf.artist, inf.artist[0] && inf.album[0] ? " - " : "", inf.album);
        if (!sub[0]) snprintf(sub, sizeof(sub), "%d Hz  %dch  %d kbps", inf.sample_rate, inf.channels, inf.bitrate / 1000);
        lv_label_set_text(s_sub, sub);
        char a[16], b[16]; ui_format_duration(a, 16, inf.position_ms / 1000); ui_format_duration(b, 16, inf.duration_ms / 1000);
        char t[40]; snprintf(t, sizeof(t), "%s / %s", a, inf.duration_ms ? b : "--:--"); lv_label_set_text(s_time, t);
        if (inf.duration_ms) lv_slider_set_value(s_slider, inf.position_ms * 100 / inf.duration_ms, LV_ANIM_OFF);
    } else { lv_label_set_text(s_title, tr(STR_MUSIC_STOPPED)); lv_label_set_text(s_sub, ""); lv_label_set_text(s_time, ""); }
    lv_label_set_text(lv_obj_get_child(s_play_btn, 0), st == AP_PLAYING ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    /* refresh() runs on a 500 ms timer, and ui_toast() replaces the previous
     * toast, so reporting the error on every tick made it flicker. Report it
     * once, when the player enters the error state. */
    static ap_state_t s_reported = AP_STOPPED;
    if (st == AP_ERROR && s_reported != AP_ERROR) ui_toastf("%s: %s", tr(STR_DECODE_FAILED), audio_player_last_error());
    s_reported = st;
}

static void play_index(int i)
{
    if (i < 0 || i >= s_ntracks) return;
    s_cur = i;
    audio_player_play(s_tracks[i].path);
    set_cover(s_tracks[i].path);
    refresh();
}
static void next_track(int dir) { if (!s_ntracks) return; int i = s_cur < 0 ? 0 : (s_cur + dir + s_ntracks) % s_ntracks; play_index(i); }

static volatile bool s_user_stopped = false;
static void ev_async(void *arg) { (void)arg; refresh(); ui_music_bar_update(); }
static void auto_next(void *arg)
{
    (void)arg;
    if (s_user_stopped) { s_user_stopped = false; return; }
    next_track(1);
}
static void ap_event(ap_state_t st, void *user)
{
    (void)user;
    ui_async(ev_async, (void *)(intptr_t)st);
    if (st == AP_STOPPED && !s_user_stopped) ui_async(auto_next, NULL);
}

static void track_cb(lv_event_t *e) { play_index((int)(intptr_t)lv_event_get_user_data(e)); }
static void track_long_cb(lv_event_t *e) { int i = (int)(intptr_t)lv_event_get_user_data(e); fav_toggle(s_tracks[i].path); if (s_show_fav) ui_nav_refresh(); }
/* Swipe left/right for the next and previous track, the way a phone player
 * behaves; that costs this screen the swipe-back gesture. */
static void music_gesture_cb(lv_dir_t dir)
{
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) return;
    s_user_stopped = true;
    next_track(dir == LV_DIR_LEFT ? 1 : -1);
    s_user_stopped = false;
}

static void play_cb(lv_event_t *e) { (void)e; if (audio_player_state() == AP_STOPPED && s_cur < 0) next_track(1); else audio_player_toggle(); refresh(); }
static void stop_cb(lv_event_t *e) { (void)e; s_user_stopped = true; audio_player_stop(); refresh(); }
static void prev_cb(lv_event_t *e) { (void)e; s_user_stopped = true; next_track(-1); s_user_stopped = false; }
static void nxt_cb(lv_event_t *e) { (void)e; s_user_stopped = true; next_track(1); s_user_stopped = false; }
static void fav_cb(lv_event_t *e) { (void)e; const char *c = audio_player_current(); if (c[0]) fav_toggle(c); }
static void mode_cb(lv_event_t *e) { (void)e; s_show_fav = !s_show_fav; ui_nav_refresh(); }
static void clear_fav_yes(bool yes, void *ud) { (void)ud; if (yes) { s_nfav = 0; fav_save(); ui_nav_refresh(); } }
static void clear_fav_cb(lv_event_t *e) { (void)e; ui_confirm(tr(STR_CLEAR_FAV), NULL, clear_fav_yes, NULL); }
static void vol_cb(lv_event_t *e) { audio_player_set_volume(lv_slider_get_value(lv_event_get_target_obj(e))); if (lv_event_get_code(e) == LV_EVENT_RELEASED) settings_save(); }
static void tick_cb(lv_timer_t *t) { (void)t; refresh(); }
static void del_cb(lv_event_t *e) { (void)e; if (s_cover_dsc) { image_loader_free(s_cover_dsc); s_cover_dsc = NULL; } s_scr = NULL; }

lv_obj_t *ui_music_create(void *arg)
{
    const char *start = arg;
    static bool cb_set = false;
    if (!cb_set) { audio_player_set_event_cb(ap_event, NULL); cb_set = true; }
    fav_load();
    build_list();
    s_scr = ui_screen_base();
    ui_screen_set_gesture(s_scr, music_gesture_cb);
    lv_obj_add_event_cb(s_scr, del_cb, LV_EVENT_DELETE, NULL);
    lv_obj_t *bar = ui_topbar(s_scr, tr(s_show_fav ? STR_FAVORITES : STR_MUSIC_PLAYER), true);
    ui_topbar_add_button(bar, s_show_fav ? LV_SYMBOL_LIST : LV_SYMBOL_OK, mode_cb, NULL);
    if (s_show_fav) ui_topbar_add_button(bar, LV_SYMBOL_TRASH, clear_fav_cb, NULL);
    lv_obj_t *c = ui_content(s_scr);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(c, ui_theme_pad(14), 0);
    /* left: now playing */
    lv_obj_t *left = ui_card(c);
    lv_obj_set_width(left, LV_PCT(40)); lv_obj_set_height(left, LV_PCT(100));
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_cover = lv_image_create(left);
    lv_obj_set_size(s_cover, 240, 240);
    lv_obj_set_style_text_font(s_cover, FONT_ICON, 0);
    lv_obj_set_style_text_color(s_cover, UI_MUTED, 0);
    lv_obj_set_style_bg_color(s_cover, UI_BG, 0); lv_obj_set_style_bg_opa(s_cover, LV_OPA_COVER, 0); lv_obj_set_style_radius(s_cover, ui_theme_radius(12), 0);
    lv_image_set_inner_align(s_cover, LV_IMAGE_ALIGN_CENTER);
    s_title = ui_label(left, "", FONT_BODY, UI_TEXT); lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    s_sub = ui_label(left, "", FONT_SMALL, UI_MUTED); lv_obj_set_style_text_align(s_sub, LV_TEXT_ALIGN_CENTER, 0);
    s_slider = lv_slider_create(left); lv_obj_set_width(s_slider, LV_PCT(90)); lv_slider_set_range(s_slider, 0, 100); lv_obj_remove_flag(s_slider, LV_OBJ_FLAG_CLICKABLE);
    s_time = ui_label(left, "", FONT_SMALL, UI_MUTED); lv_obj_set_style_text_align(s_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *row = ui_row(left);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_button_colored(row, LV_SYMBOL_PREV, UI_CARD_HI, prev_cb, NULL);
    s_play_btn = ui_button(row, LV_SYMBOL_PLAY, play_cb, NULL);
    ui_button_colored(row, LV_SYMBOL_STOP, UI_CARD_HI, stop_cb, NULL);
    ui_button_colored(row, LV_SYMBOL_NEXT, UI_CARD_HI, nxt_cb, NULL);
    ui_button_colored(row, LV_SYMBOL_OK, UI_CARD_HI, fav_cb, NULL);
    lv_obj_t *vrow = ui_row(left);
    lv_obj_t *vl = lv_label_create(vrow); lv_label_set_text(vl, LV_SYMBOL_VOLUME_MAX);
    lv_obj_t *vs = lv_slider_create(vrow); lv_obj_set_width(vs, LV_PCT(70)); lv_slider_set_range(vs, 0, 100); lv_slider_set_value(vs, audio_player_volume(), LV_ANIM_OFF);
    lv_obj_add_event_cb(vs, vol_cb, LV_EVENT_VALUE_CHANGED, NULL); lv_obj_add_event_cb(vs, vol_cb, LV_EVENT_RELEASED, NULL);
    /* right: list */
    s_list = lv_obj_create(c);
    lv_obj_set_flex_grow(s_list, 1); lv_obj_set_height(s_list, LV_PCT(100));
    lv_obj_set_style_bg_opa(s_list, 0, 0); lv_obj_set_style_border_width(s_list, 0, 0); lv_obj_set_style_pad_all(s_list, ui_theme_pad(0), 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(s_list, ui_theme_pad(6), 0);
    if (!s_ntracks) ui_label(s_list, tr(STR_NO_AUDIO), FONT_BODY, UI_MUTED);
    for (int i = 0; i < s_ntracks; i++) {
        char sub[80]; const char *p = s_tracks[i].path + strlen(SD_MOUNT); const char *sl = strrchr(p, '/'); size_t dl = sl ? (size_t)(sl - p) : 0; if (dl >= sizeof(sub)) dl = sizeof(sub) - 1; memcpy(sub, p, dl); sub[dl] = 0;
        lv_obj_t *it = ui_list_item(s_list, fav_find(s_tracks[i].path) >= 0 ? LV_SYMBOL_OK : LV_SYMBOL_AUDIO, ui_basename(s_tracks[i].path), sub, track_cb, (void *)(intptr_t)i);
        lv_obj_add_event_cb(it, track_long_cb, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);
        if (i == s_cur) {
            lv_obj_set_style_bg_color(it, UI_CARD_HI, 0);
            lv_obj_set_style_border_color(it, UI_ACCENT, 0);
        }
    }
    if (start && start[0]) { for (int i = 0; i < s_ntracks; i++) if (!strcmp(s_tracks[i].path, start)) s_cur = i; set_cover(start); }
    else if (audio_player_current()[0]) set_cover(audio_player_current());
    refresh();
    ui_timer_bind(lv_timer_create(tick_cb, 500, NULL), s_scr);
    return s_scr;
}

/* ---- background bar (on the top layer, hidden on the music screen itself) ---- */
static lv_obj_t *s_bar = NULL, *s_bar_lbl = NULL;
static void bar_open_cb(lv_event_t *e) { (void)e; ui_nav_push(ui_music_create, NULL); }
static void bar_toggle_cb(lv_event_t *e) { lv_event_stop_bubbling(e); audio_player_toggle(); ui_music_bar_update(); }
static void bar_stop_cb(lv_event_t *e) { lv_event_stop_bubbling(e); s_user_stopped = true; audio_player_stop(); ui_music_bar_update(); }
void ui_music_bar_update(void)
{
    ap_state_t st = audio_player_state();
    bool show = (st == AP_PLAYING || st == AP_PAUSED) && !ui_nav_is_top(ui_music_create) && !ui_nav_is_top(ui_terminal_create) && !ui_screensaver_active();
    if (!show) { if (s_bar) lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN); return; }
    if (!s_bar) {
        s_bar = lv_obj_create(lv_layer_top());
        lv_obj_set_size(s_bar, LV_PCT(100), 48);
        lv_obj_align(s_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(s_bar, UI_CARD, 0);
        lv_obj_set_style_bg_opa(s_bar, LV_OPA_90, 0);
        lv_obj_set_style_border_width(s_bar, 0, 0); lv_obj_set_style_radius(s_bar, ui_theme_radius(0), 0);
        lv_obj_set_style_pad_hor(s_bar, ui_theme_pad(12), 0); lv_obj_set_style_pad_ver(s_bar, ui_theme_pad(4), 0);
        lv_obj_set_flex_flow(s_bar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(s_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_bar, bar_open_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *ic = lv_label_create(s_bar); lv_label_set_text(ic, LV_SYMBOL_AUDIO); lv_obj_set_style_text_color(ic, UI_ACCENT2, 0);
        s_bar_lbl = lv_label_create(s_bar); lv_obj_set_flex_grow(s_bar_lbl, 1); lv_label_set_long_mode(s_bar_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_style_text_font(s_bar_lbl, FONT_SMALL, 0);
        lv_obj_t *b1 = lv_button_create(s_bar); lv_obj_set_size(b1, 44, 36); lv_obj_set_style_bg_color(b1, UI_CARD_HI, 0); lv_obj_set_style_shadow_width(b1, 0, 0);
        lv_obj_t *l1 = lv_label_create(b1); lv_label_set_text(l1, LV_SYMBOL_PAUSE); lv_obj_center(l1); lv_obj_add_event_cb(b1, bar_toggle_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *b2 = lv_button_create(s_bar); lv_obj_set_size(b2, 44, 36); lv_obj_set_style_bg_color(b2, UI_CARD_HI, 0); lv_obj_set_style_shadow_width(b2, 0, 0);
        lv_obj_t *l2 = lv_label_create(b2); lv_label_set_text(l2, LV_SYMBOL_STOP); lv_obj_center(l2); lv_obj_add_event_cb(b2, bar_stop_cb, LV_EVENT_CLICKED, NULL);
    }
    /* This bar survives navigation and theme changes on the top layer. */
    lv_obj_set_style_bg_color(s_bar, UI_CARD, 0);
    lv_obj_set_style_text_color(s_bar, UI_TEXT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(s_bar, 0), UI_ACCENT2, 0);
    for (int i = 2; i <= 3; i++)
        lv_obj_set_style_bg_color(lv_obj_get_child(s_bar, i), UI_CARD_HI, 0);
    ap_info_t inf; audio_player_info(&inf);
    char t[200]; snprintf(t, sizeof(t), "%s%s%s", inf.title[0] ? inf.title : ui_basename(audio_player_current()), inf.artist[0] ? " - " : "", inf.artist);
    lv_label_set_text(s_bar_lbl, t);
    lv_label_set_text(lv_obj_get_child(lv_obj_get_child(s_bar, 2), 0), st == AP_PLAYING ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
}
