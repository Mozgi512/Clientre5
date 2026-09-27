#include "ui.h"
#include "storage/sd.h"
#include "media/exif.h"
#include "media/image_loader.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

typedef struct {
    char path[300];
    lv_obj_t *scr, *img, *cont, *info, *status;
    lv_image_dsc_t *dsc;
    int zoom;   /* 256 = 1:1 */
    int rot;    /* degrees */
    exif_info_t exif;
} iv_t;
static iv_t s_iv;

static bool is_img(const char *n) { const char *x = fs_ext(n); return !strcmp(x, "jpg") || !strcmp(x, "jpeg") || !strcmp(x, "png") || !strcmp(x, "bmp") || !strcmp(x, "gif"); }

static void free_img(void)
{
    if (s_iv.scr && s_iv.img) lv_image_set_src(s_iv.img, NULL);
    if (s_iv.dsc) { image_loader_free(s_iv.dsc); s_iv.dsc = NULL; }
}

static void apply_transform(void)
{
    if (!s_iv.scr) return;
    lv_image_set_rotation(s_iv.img, s_iv.rot * 10);
    lv_image_set_scale(s_iv.img, s_iv.zoom);
    lv_obj_center(s_iv.img);
}

static void fit(void)
{
    int32_t W = lv_obj_get_width(s_iv.cont), H = lv_obj_get_height(s_iv.cont);
    int32_t iw = lv_image_get_src_width(s_iv.img), ih = lv_image_get_src_height(s_iv.img);
    if (iw <= 0 || ih <= 0) return;
    bool swap = (s_iv.rot % 180) != 0;
    int32_t ew = swap ? ih : iw, eh = swap ? iw : ih;
    int z = LV_MIN(W * 256 / ew, H * 256 / eh);
    if (z > 256) z = 256;
    if (z < 16) z = 16;
    s_iv.zoom = z;
    apply_transform();
}

static void load(const char *path)
{
    free_img();
    strlcpy(s_iv.path, path, sizeof(s_iv.path));
    lv_label_set_text(lv_obj_get_child(lv_obj_get_child(s_iv.scr, 0), 1), ui_basename(path));
    char status[96] = "";
    const char *x = fs_ext(path);
    s_iv.rot = 0;
    exif_parse_file(path, &s_iv.exif);
    int32_t W = lv_display_get_horizontal_resolution(NULL), H = lv_display_get_vertical_resolution(NULL);
    /* Decoding runs on this task, so say so before blocking on a big file. */
    lv_obj_t *busy = ui_busy(tr(STR_PLEASE_WAIT));
    lv_refr_now(NULL);
    if (!strcmp(x, "jpg") || !strcmp(x, "jpeg")) {
        s_iv.dsc = image_loader_jpeg(path, W * 2, H * 2, status, sizeof(status));
        if (s_iv.dsc) lv_image_set_src(s_iv.img, s_iv.dsc);
        switch (s_iv.exif.orientation) { case 3: s_iv.rot = 180; break; case 6: s_iv.rot = 90; break; case 8: s_iv.rot = 270; break; default: break; }
    } else if (!strcmp(x, "png")) {
        s_iv.dsc = image_loader_png(path, W * 2, H * 2, status, sizeof(status));
        if (s_iv.dsc) {
            lv_image_set_src(s_iv.img, s_iv.dsc);
        } else {
            /* Interlaced or an exotic colour type: let LVGL try. */
            char src[320]; snprintf(src, sizeof(src), "S:%s", path);
            lv_image_set_src(s_iv.img, src);
            strlcat(status, ", retried with the LVGL decoder", sizeof(status));
        }
    } else {
        char src[320]; snprintf(src, sizeof(src), "S:%s", path);
        lv_image_set_src(s_iv.img, src);
        snprintf(status, sizeof(status), "%s via LVGL decoder", x);
    }
    ui_busy_close(busy);
    if (lv_image_get_src_width(s_iv.img) <= 0) { lv_label_set_text(s_iv.status, tr(STR_IMAGE_LOAD_FAILED)); }
    else {
        char desc[400]; exif_describe(&s_iv.exif, desc, sizeof(desc));
        char all[520]; snprintf(all, sizeof(all), "%s\n%s", desc[0] ? desc : tr(STR_NO_EXIF), status);
        lv_label_set_text(s_iv.info, all);
        lv_label_set_text(s_iv.status, status);
    }
    lv_obj_update_layout(s_iv.cont);
    fit();
}

static void step(int dir)
{
    char dir_path[300]; strlcpy(dir_path, s_iv.path, sizeof(dir_path));
    char *sl = strrchr(dir_path, '/'); if (!sl) return; *sl = 0;
    DIR *d = opendir(dir_path); if (!d) return;
    /* The LVGL task has a 16 KB stack, so this list lives on the heap. */
    char (*names)[256] = malloc(400 * 256);
    if (!names) { closedir(d); return; }
    int n = 0; struct dirent *de;
    while ((de = readdir(d)) && n < 400) if (is_img(de->d_name) && de->d_name[0] != '.') strlcpy(names[n++], de->d_name, 256);
    closedir(d);
    if (!n) { free(names); return; }
    /* sort */
    for (int i = 0; i < n - 1; i++) for (int j = i + 1; j < n; j++) if (strcasecmp(names[i], names[j]) > 0) { char t[256]; memcpy(t, names[i], 256); memcpy(names[i], names[j], 256); memcpy(names[j], t, 256); }
    int cur = 0; for (int i = 0; i < n; i++) if (!strcmp(names[i], ui_basename(s_iv.path))) cur = i;
    cur = (cur + dir + n) % n;
    char p[600]; snprintf(p, sizeof(p), "%s/%s", dir_path, names[cur]);
    free(names);
    load(p);
}

/* The horizontal axis pages through the folder, so this screen leaves the
 * swipe-back gesture alone and uses the Back button instead. */
static void gesture_cb(lv_dir_t dir)
{
    if (dir == LV_DIR_LEFT) step(1); else if (dir == LV_DIR_RIGHT) step(-1);
}
static void zoom_in_cb(lv_event_t *e) { (void)e; s_iv.zoom = LV_MIN(s_iv.zoom * 3 / 2, 2048); apply_transform(); }
static void zoom_out_cb(lv_event_t *e) { (void)e; s_iv.zoom = LV_MAX(s_iv.zoom * 2 / 3, 16); apply_transform(); }
static void fit_cb(lv_event_t *e) { (void)e; fit(); }
static void rot_cb(lv_event_t *e) { (void)e; s_iv.rot = (s_iv.rot + 90) % 360; fit(); }
static void info_cb(lv_event_t *e) { (void)e; if (lv_obj_has_flag(s_iv.info, LV_OBJ_FLAG_HIDDEN)) lv_obj_remove_flag(s_iv.info, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s_iv.info, LV_OBJ_FLAG_HIDDEN); }
static void prev_cb(lv_event_t *e) { (void)e; step(-1); }
static void next_cb(lv_event_t *e) { (void)e; step(1); }
static void pan_key_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    lv_obj_scroll_by(lv_event_get_target_obj(e),
        key == LV_KEY_LEFT ? 40 : key == LV_KEY_RIGHT ? -40 : 0,
        key == LV_KEY_UP ? 40 : key == LV_KEY_DOWN ? -40 : 0, LV_ANIM_OFF);
}
static void del_cb(lv_event_t *e) { (void)e; free_img(); s_iv.scr = NULL; s_iv.img = NULL; }

lv_obj_t *ui_image_create(void *arg)
{
    const char *path = arg;
    lv_obj_t *scr = ui_screen_base();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    s_iv.scr = scr;
    lv_obj_add_event_cb(scr, del_cb, LV_EVENT_DELETE, NULL);
    lv_obj_t *bar = ui_topbar(scr, "", true);
    ui_topbar_add_button(bar, LV_SYMBOL_LEFT, prev_cb, NULL);
    ui_topbar_add_button(bar, LV_SYMBOL_RIGHT, next_cb, NULL);
    ui_topbar_add_button(bar, LV_SYMBOL_MINUS, zoom_out_cb, NULL);
    ui_topbar_add_button(bar, LV_SYMBOL_PLUS, zoom_in_cb, NULL);
    ui_topbar_add_button(bar, LV_SYMBOL_IMAGE, fit_cb, NULL);
    ui_topbar_add_button(bar, LV_SYMBOL_REFRESH, rot_cb, NULL);
    ui_topbar_add_button(bar, LV_SYMBOL_LIST, info_cb, NULL);
    s_iv.cont = lv_obj_create(scr);
    lv_obj_add_event_cb(s_iv.cont, pan_key_cb, LV_EVENT_KEY, NULL);
    lv_obj_set_pos(s_iv.cont, 0, UI_TOPBAR_H);
    lv_obj_set_size(s_iv.cont, LV_PCT(100), lv_display_get_vertical_resolution(NULL) - UI_TOPBAR_H);
    lv_obj_set_style_bg_color(s_iv.cont, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_iv.cont, 0, 0);
    lv_obj_set_style_pad_all(s_iv.cont, 0, 0);
    lv_obj_set_style_radius(s_iv.cont, 0, 0);
    lv_obj_set_scrollbar_mode(s_iv.cont, LV_SCROLLBAR_MODE_OFF);
    ui_screen_set_gesture(scr, gesture_cb);
    s_iv.img = lv_image_create(s_iv.cont);
    lv_image_set_inner_align(s_iv.img, LV_IMAGE_ALIGN_CENTER);
    lv_obj_center(s_iv.img);
    s_iv.info = lv_label_create(scr);
    lv_obj_set_style_bg_color(s_iv.info, UI_CARD, 0);
    lv_obj_set_style_bg_opa(s_iv.info, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(s_iv.info, 10, 0);
    lv_obj_set_style_text_font(s_iv.info, FONT_SMALL, 0);
    lv_obj_align(s_iv.info, LV_ALIGN_TOP_LEFT, 10, UI_TOPBAR_H + 10);
    lv_obj_add_flag(s_iv.info, LV_OBJ_FLAG_HIDDEN);
    s_iv.status = lv_label_create(scr);
    lv_obj_set_style_text_font(s_iv.status, FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_iv.status, UI_MUTED, 0);
    lv_obj_align(s_iv.status, LV_ALIGN_BOTTOM_LEFT, 10, -6);
    if (path) { lv_label_set_text(s_iv.status, tr(STR_LOADING_IMAGE)); load(path); }
    return scr;
}
