/* TF card / USB file manager. */
#include "ui.h"
#include "ui_softkbd.h"
#include "storage/sd.h"
#include "sys/usb_mgr.h"
#include "media/audio_player.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "files";

typedef struct { char name[256]; bool dir; uint64_t size; } entry_t;
typedef struct {
    char path[300];
    entry_t *items; int n;
    bool select_mode; uint8_t *sel;
    lv_obj_t *scr, *list, *title, *footer, *selbar;
} fm_t;
static fm_t s_fm;

/* clipboard */
static char s_clip[16][300]; static int s_clip_n = 0; static bool s_clip_cut = false;

static int cmp_entry(const void *a, const void *b)
{
    const entry_t *x = a, *y = b;
    if (x->dir != y->dir) return x->dir ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

static void load_dir(void)
{
    free(s_fm.items); s_fm.items = NULL; s_fm.n = 0;
    free(s_fm.sel); s_fm.sel = NULL;
    DIR *d = opendir(s_fm.path);
    if (!d) return;
    int cap = 64; s_fm.items = malloc(cap * sizeof(entry_t));
    struct dirent *de;
    char full[600];
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (de->d_name[0] == '.' ) continue;
        if (s_fm.n == cap) { cap *= 2; s_fm.items = realloc(s_fm.items, cap * sizeof(entry_t)); }
        entry_t *e = &s_fm.items[s_fm.n++];
        strlcpy(e->name, de->d_name, sizeof(e->name));
        e->dir = (de->d_type == DT_DIR);
        e->size = 0;
        if (!e->dir) { snprintf(full, sizeof(full), "%s/%s", s_fm.path, de->d_name); struct stat st; if (stat(full, &st) == 0) { e->size = st.st_size; e->dir = S_ISDIR(st.st_mode); } }
    }
    closedir(d);
    qsort(s_fm.items, s_fm.n, sizeof(entry_t), cmp_entry);
    s_fm.sel = calloc(s_fm.n, 1);
}

static const char *icon_for(const entry_t *e)
{
    if (e->dir) return LV_SYMBOL_DIRECTORY;
    const char *x = fs_ext(e->name);
    if (!strcmp(x, "jpg") || !strcmp(x, "jpeg") || !strcmp(x, "png") || !strcmp(x, "bmp") || !strcmp(x, "gif")) return LV_SYMBOL_IMAGE;
    if (!strcmp(x, "mp3") || !strcmp(x, "flac") || !strcmp(x, "wav")) return LV_SYMBOL_AUDIO;
    if (!strcmp(x, "bin")) return LV_SYMBOL_DOWNLOAD;
    return LV_SYMBOL_FILE;
}
static bool is_image(const char *n) { const char *x = fs_ext(n); return !strcmp(x, "jpg") || !strcmp(x, "jpeg") || !strcmp(x, "png") || !strcmp(x, "bmp") || !strcmp(x, "gif"); }
static bool is_audio(const char *n) { const char *x = fs_ext(n); return !strcmp(x, "mp3") || !strcmp(x, "flac") || !strcmp(x, "wav"); }
static bool is_text(const char *n)
{
    static const char *t[] = { "txt","log","md","json","ini","cfg","conf","csv","c","h","cpp","hpp","py","js","html","css","xml","yaml","yml","sh","pub","", NULL };
    const char *x = fs_ext(n);
    for (int i = 0; t[i]; i++) if (!strcmp(x, t[i])) return true;
    return false;
}

static void rebuild(void);
static void open_path(const char *path) { strlcpy(s_fm.path, path, sizeof(s_fm.path)); s_fm.select_mode = false; load_dir(); rebuild(); }

static char *s_arg_path = NULL;
static void item_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    entry_t *it = &s_fm.items[i];
    if (s_fm.select_mode) { s_fm.sel[i] = !s_fm.sel[i]; rebuild(); return; }
    char full[600]; snprintf(full, sizeof(full), "%s/%s", s_fm.path, it->name);
    if (it->dir) { open_path(full); return; }
    free(s_arg_path); s_arg_path = strdup(full);
    if (is_image(it->name)) ui_nav_push(ui_image_create, s_arg_path);
    else if (is_audio(it->name)) { audio_player_play(full); ui_nav_push(ui_music_create, s_arg_path); }
    else if (is_text(it->name) || it->size < 256 * 1024) ui_nav_push(ui_text_create, s_arg_path);
    else ui_toast(tr(STR_FILE_TOO_LARGE));
}

/* ---- operations ---- */
static void rename_cb(const char *name, void *ud)
{
    int i = (int)(intptr_t)ud;
    if (!name || !*name || strchr(name, '/')) return;
    char a[600], b[600];
    snprintf(a, sizeof(a), "%s/%s", s_fm.path, s_fm.items[i].name);
    snprintf(b, sizeof(b), "%s/%s", s_fm.path, name);
    if (fs_exists(b)) { ui_toast(tr(STR_TARGET_EXISTS)); return; }
    if (rename(a, b) != 0) ui_toastf(tr(STR_OP_FAILED), strerror(errno));
    load_dir(); rebuild();
}
static void del_one_cb(bool yes, void *ud)
{
    if (!yes) return;
    int i = (int)(intptr_t)ud;
    char a[600]; snprintf(a, sizeof(a), "%s/%s", s_fm.path, s_fm.items[i].name);
    int rc = fs_remove_recursive(a);
    if (rc) ui_toastf(tr(STR_OP_FAILED), strerror(-rc));
    load_dir(); rebuild();
}
static void clip_set(int i, bool cut)
{
    s_clip_n = 0; s_clip_cut = cut;
    if (i >= 0) { snprintf(s_clip[0], 300, "%s/%s", s_fm.path, s_fm.items[i].name); s_clip_n = 1; }
    else for (int k = 0; k < s_fm.n && s_clip_n < 16; k++) if (s_fm.sel[k]) { snprintf(s_clip[s_clip_n++], 300, "%s/%s", s_fm.path, s_fm.items[k].name); }
    ui_toastf("%s: %d", cut ? tr(STR_CUT) : tr(STR_COPY), s_clip_n);
}
static void item_menu_cb(int m, void *ud)
{
    int i = (int)(intptr_t)ud;
    char q[300];
    switch (m) {
    case 0: ui_prompt(tr(STR_RENAME), tr(STR_FILE_NAME), s_fm.items[i].name, false, rename_cb, ud); break;
    case 1: clip_set(i, false); break;
    case 2: clip_set(i, true); break;
    case 3: snprintf(q, sizeof(q), tr(STR_DELETE_Q), 1); ui_confirm(tr(STR_DELETE), q, del_one_cb, ud); break;
    case 4: s_fm.select_mode = true; s_fm.sel[i] = 1; rebuild(); break;
    }
}
static void item_long_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    const char *items[] = { tr(STR_RENAME), tr(STR_COPY), tr(STR_CUT), tr(STR_DELETE), tr(STR_SELECT) };
    ui_menu(s_fm.items[i].name, items, 5, item_menu_cb, (void *)(intptr_t)i);
}

typedef struct { lv_obj_t *busy; volatile bool done; int rc; } op_ctx_t;
static void progress_cb(uint64_t done, uint64_t total, void *ud) { (void)done; (void)total; (void)ud; }
static void paste_task(void *arg)
{
    op_ctx_t *op = arg;
    int rc = 0;
    for (int k = 0; k < s_clip_n && rc == 0; k++) {
        const char *src = s_clip[k];
        char dst[600]; snprintf(dst, sizeof(dst), "%s/%s", s_fm.path, ui_basename(src));
        if (!strcmp(src, dst)) continue;
        if (fs_is_dir(src) && !strncmp(dst, src, strlen(src)) && dst[strlen(src)] == '/') { rc = -EINVAL; break; }
        if (fs_exists(dst)) { char u[600]; fs_unique_name(s_fm.path, ui_basename(src), u, sizeof(u)); strlcpy(dst, u, sizeof(dst)); }
        if (s_clip_cut) {
            if (rename(src, dst) == 0) continue;
            rc = fs_copy_recursive(src, dst, progress_cb, NULL);
            if (rc == 0) fs_remove_recursive(src);
        } else rc = fs_copy_recursive(src, dst, progress_cb, NULL);
    }
    op->rc = rc; op->done = true;
    vTaskDelete(NULL);
}
static void paste_poll(lv_timer_t *t)
{
    op_ctx_t *op = lv_timer_get_user_data(t);
    if (!op->done) return;
    lv_timer_delete(t);
    ui_busy_close(op->busy);
    if (op->rc == -EINVAL) ui_toast(tr(STR_INTO_ITSELF));
    else if (op->rc) ui_toastf(tr(STR_OP_FAILED), strerror(-op->rc));
    else ui_toast(tr(STR_PASTE_DONE));
    if (s_clip_cut) s_clip_n = 0;
    free(op);
    load_dir(); rebuild();
}
static void paste_cb(lv_event_t *e)
{
    (void)e;
    if (!s_clip_n) { ui_toast(tr(STR_CLIPBOARD_EMPTY)); return; }
    op_ctx_t *op = calloc(1, sizeof(*op));
    op->busy = ui_busy(tr(STR_PLEASE_WAIT));
    xTaskCreate(paste_task, "paste", 6144, op, 3, NULL);
    lv_timer_create(paste_poll, 200, op);
}

static void mkdir_cb(const char *name, void *ud)
{
    (void)ud; if (!name || !*name) return;
    char p[600]; snprintf(p, sizeof(p), "%s/%s", s_fm.path, name);
    if (mkdir(p, 0777) != 0) ui_toastf(tr(STR_OP_FAILED), strerror(errno));
    load_dir(); rebuild();
}
static void newfile_cb(const char *name, void *ud)
{
    (void)ud; if (!name || !*name) return;
    char p[600]; snprintf(p, sizeof(p), "%s/%s", s_fm.path, name);
    if (fs_exists(p)) { ui_toast(tr(STR_TARGET_EXISTS)); return; }
    FILE *f = fopen(p, "wb"); if (f) fclose(f); else ui_toastf(tr(STR_OP_FAILED), strerror(errno));
    load_dir(); rebuild();
}
static void del_sel_cb(bool yes, void *ud)
{
    (void)ud; if (!yes) return;
    int rc = 0;
    for (int k = 0; k < s_fm.n; k++) if (s_fm.sel[k]) { char a[600]; snprintf(a, sizeof(a), "%s/%s", s_fm.path, s_fm.items[k].name); int r = fs_remove_recursive(a); if (r && !rc) rc = r; }
    if (rc) ui_toastf(tr(STR_OP_FAILED), strerror(-rc));
    s_fm.select_mode = false; load_dir(); rebuild();
}
static void toolbar_menu_cb(int m, void *ud)
{
    (void)ud;
    switch (m) {
    case 0: ui_prompt(tr(STR_NEW_FOLDER), tr(STR_FOLDER_NAME), "", false, mkdir_cb, NULL); break;
    case 1: ui_prompt(tr(STR_NEW_FILE), tr(STR_FILE_NAME), "", false, newfile_cb, NULL); break;
    case 2: paste_cb(NULL); break;
    case 3: s_fm.select_mode = true; rebuild(); break;
    case 4: open_path(usb_mgr_msc_mounted() ? USB_MOUNT : SD_MOUNT); break;
    }
}
static void toolbar_cb(lv_event_t *e)
{
    (void)e;
    const char *items[] = { tr(STR_NEW_FOLDER), tr(STR_NEW_FILE), tr(STR_PASTE), tr(STR_SELECT), usb_mgr_msc_mounted() ? tr(STR_USB_DRIVE) : tr(STR_TF_CARD) };
    ui_menu(tr(STR_FILES), items, 5, toolbar_menu_cb, NULL);
}
static void up_cb(lv_event_t *e)
{
    (void)e;
    if (!strcmp(s_fm.path, SD_MOUNT) || !strcmp(s_fm.path, USB_MOUNT)) { ui_nav_back(); return; }
    char *p = strrchr(s_fm.path, '/');
    if (p && p != s_fm.path) *p = 0;
    s_fm.select_mode = false; load_dir(); rebuild();
}
static void sel_all_cb(lv_event_t *e) { (void)e; bool all = true; for (int k = 0; k < s_fm.n; k++) if (!s_fm.sel[k]) all = false; for (int k = 0; k < s_fm.n; k++) s_fm.sel[k] = !all; rebuild(); }
static void sel_cancel_cb(lv_event_t *e) { (void)e; s_fm.select_mode = false; memset(s_fm.sel, 0, s_fm.n); rebuild(); }
static void sel_copy_cb(lv_event_t *e) { (void)e; clip_set(-1, false); s_fm.select_mode = false; rebuild(); }
static void sel_cut_cb(lv_event_t *e) { (void)e; clip_set(-1, true); s_fm.select_mode = false; rebuild(); }
static void sel_del_cb(lv_event_t *e)
{
    (void)e; int n = 0; for (int k = 0; k < s_fm.n; k++) n += s_fm.sel[k];
    if (!n) { ui_toast(tr(STR_SELECT)); return; }
    char q[200]; snprintf(q, sizeof(q), tr(STR_DELETE_Q), n);
    ui_confirm(tr(STR_DELETE_SELECTED), q, del_sel_cb, NULL);
}

static void rebuild(void)
{
    if (!s_fm.scr) return;
    lv_label_set_text(s_fm.title, s_fm.path);
    lv_obj_clean(s_fm.list);
    if (s_fm.n == 0) ui_label(s_fm.list, tr(STR_EMPTY_FOLDER), FONT_BODY, UI_MUTED);
    uint64_t total = 0;
    for (int i = 0; i < s_fm.n; i++) {
        entry_t *e = &s_fm.items[i];
        char sub[48] = "";
        if (!e->dir) { ui_format_size(sub, sizeof(sub), e->size); total += e->size; }
        lv_obj_t *it = ui_list_item(s_fm.list, icon_for(e), e->name, sub, item_cb, (void *)(intptr_t)i);
        lv_obj_add_event_cb(it, item_long_cb, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);
        if (s_fm.select_mode) {
            lv_obj_t *cb = lv_checkbox_create(it);
            lv_checkbox_set_text(cb, "");
            if (s_fm.sel[i]) lv_obj_add_state(cb, LV_STATE_CHECKED);
            lv_obj_remove_flag(cb, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    char a[48], b[48], line[200]; uint64_t tot = 0, fr = 0;
    ui_format_size(a, sizeof(a), total);
    const char *root = strncmp(s_fm.path, USB_MOUNT, strlen(USB_MOUNT)) == 0 ? USB_MOUNT : SD_MOUNT;
    if (sd_space(root, &tot, &fr)) { char f1[32], f2[32]; ui_format_size(f1, 32, fr); ui_format_size(f2, 32, tot); snprintf(b, sizeof(b), tr(STR_FREE_SPACE), f1, f2); } else b[0] = 0;
    snprintf(line, sizeof(line), tr(STR_ITEMS_TOTAL), (unsigned)s_fm.n, a);
    strlcat(line, "   ", sizeof(line)); strlcat(line, b, sizeof(line));
    lv_label_set_text(s_fm.footer, line);
    if (s_fm.select_mode) lv_obj_remove_flag(s_fm.selbar, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s_fm.selbar, LV_OBJ_FLAG_HIDDEN);
}

static void del_cb(lv_event_t *e) { (void)e; free(s_fm.items); s_fm.items = NULL; free(s_fm.sel); s_fm.sel = NULL; s_fm.scr = NULL; }

lv_obj_t *ui_files_create(void *arg)
{
    const char *path = arg;
    if (!path || !*path) path = s_fm.path[0] ? s_fm.path : SD_MOUNT;
    if (!sd_is_mounted() && !strncmp(path, SD_MOUNT, strlen(SD_MOUNT)) && !sd_mount()) {
        if (usb_mgr_msc_mounted()) path = USB_MOUNT;
    }
    lv_obj_t *scr = ui_screen_base();
    s_fm.scr = scr;
    lv_obj_add_event_cb(scr, del_cb, LV_EVENT_DELETE, NULL);
    lv_obj_t *bar = ui_topbar(scr, "", true);
    s_fm.title = lv_obj_get_child(bar, 1);
    lv_obj_set_style_text_font(s_fm.title, FONT_BODY, 0);
    ui_topbar_add_button(bar, LV_SYMBOL_UP, up_cb, NULL);
    ui_topbar_add_button(bar, LV_SYMBOL_PASTE, paste_cb, NULL);
    ui_topbar_add_button(bar, LV_SYMBOL_PLUS, toolbar_cb, NULL);
    lv_obj_t *c = ui_content(scr);
    lv_obj_set_style_pad_row(c, ui_theme_pad(6), 0);
    s_fm.selbar = ui_row(c);
    ui_button_colored(s_fm.selbar, tr(STR_SELECT_ALL), UI_CARD_HI, sel_all_cb, NULL);
    ui_button_colored(s_fm.selbar, tr(STR_COPY), UI_CARD_HI, sel_copy_cb, NULL);
    ui_button_colored(s_fm.selbar, tr(STR_CUT), UI_CARD_HI, sel_cut_cb, NULL);
    ui_button_colored(s_fm.selbar, tr(STR_DELETE_SELECTED), UI_DANGER, sel_del_cb, NULL);
    ui_button_colored(s_fm.selbar, tr(STR_CANCEL_SELECT), UI_CARD_HI, sel_cancel_cb, NULL);
    s_fm.footer = ui_label(c, "", FONT_SMALL, UI_MUTED);
    s_fm.list = lv_obj_create(c);
    lv_obj_set_width(s_fm.list, LV_PCT(100)); lv_obj_set_height(s_fm.list, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_fm.list, 0, 0); lv_obj_set_style_border_width(s_fm.list, 0, 0); lv_obj_set_style_pad_all(s_fm.list, 0, 0);
    lv_obj_set_flex_flow(s_fm.list, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(s_fm.list, 6, 0);
    if (!sd_is_mounted() && !usb_mgr_msc_mounted()) {
        lv_label_set_text(s_fm.title, tr(STR_FILES));
        ui_label(c, tr(STR_TF_NOT_MOUNTED), FONT_BODY, UI_WARN);
        lv_obj_add_flag(s_fm.selbar, LV_OBJ_FLAG_HIDDEN);
        return scr;
    }
    open_path(path);
    return scr;
}
