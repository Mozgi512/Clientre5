/* Standby clock/calendar cards, with optional classic movable widgets. */
#include "ui.h"
#include "ui_motion.h"
#include "settings/settings.h"
#include "sys/sysinfo.h"
#include "net/wifi_mgr.h"
#include "storage/sd.h"
#include "ui_softkbd.h"
#include "media/image_loader.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <time.h>

extern bool g_ssh_session_active;
static lv_obj_t *s_scr = NULL, *s_elems[4], *s_wall;
static lv_timer_t *s_tick, *s_wall_timer;
static lv_image_dsc_t *s_wall_dsc = NULL;
static int s_wall_idx = 0;
static bool s_edit_dirty = false;
static bool s_stopping = false;

static lv_obj_t *s_clock, *s_date, *s_month, *s_battery, *s_power, *s_network, *s_ip;
static lv_obj_t *s_days[42], *s_charge;
static int s_calendar_key = -1;

static void label_changed(lv_obj_t *obj, const char *text)
{
    if (strcmp(lv_label_get_text(obj), text)) lv_label_set_text(obj, text);
}

static void standby_update(void)
{
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    char b[96];
    strftime(b, sizeof(b), "%H:%M", &tm); label_changed(s_clock, b);
    strftime(b, sizeof(b), "%Y.%m.%d  /  %A", &tm); label_changed(s_date, b);
    int key = (tm.tm_year * 12 + tm.tm_mon) * 32 + tm.tm_mday;
    if (key != s_calendar_key) {
        s_calendar_key = key;
        strftime(b, sizeof(b), "%Y / %m", &tm); label_changed(s_month, b);
        int first = (tm.tm_wday - (tm.tm_mday - 1) % 7 + 7) % 7;
        static const int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        int year = tm.tm_year + 1900, count = lengths[tm.tm_mon];
        if (tm.tm_mon == 1 && year%4 == 0 && (year%100 != 0 || year%400 == 0)) count++;
        for (int i=0;i<42;i++) {
            int day = i-first+1; bool valid=day>0 && day<=count, today=valid && day==tm.tm_mday;
            if (valid) snprintf(b,sizeof(b),"%02d",day); else b[0]=0;
            label_changed(s_days[i],b);
            lv_obj_set_style_bg_opa(s_days[i],today ? LV_OPA_COVER : LV_OPA_TRANSP,0);
            lv_obj_set_style_text_color(s_days[i],today ? UI_BG : (i%7 == 0 ? UI_ACCENT : UI_TEXT),0);
        }
    }
    int bat = sysinfo_battery_percent();
    if (bat>=0) snprintf(b,sizeof(b),"%d%%",bat); else strlcpy(b,"--",sizeof(b));
    label_changed(s_battery,b);
    int level=bat>=0 ? bat : 0;
    if (lv_bar_get_value(s_charge)!=level) lv_bar_set_value(s_charge,level,LV_ANIM_OFF);
    label_changed(s_power,sysinfo_power_src_text());
    label_changed(s_network,wifi_mgr_is_connected() ? wifi_mgr_ssid() : tr(STR_DISCONNECTED));
    wifi_mgr_get_ip4(b,sizeof(b)); label_changed(s_ip,wifi_mgr_is_connected() ? b : "--");
}

static lv_obj_t *standby_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l=lv_label_create(parent); lv_label_set_text(l,text);
    lv_obj_set_style_text_font(l,font,0); lv_obj_set_style_text_color(l,color,0);
    lv_obj_remove_flag(l,LV_OBJ_FLAG_CLICKABLE); return l;
}
static lv_obj_t *standby_card(int x,int y,int w,int h)
{
    lv_obj_t *c=lv_obj_create(s_scr); lv_obj_set_pos(c,x,y); lv_obj_set_size(c,w,h);
    lv_obj_set_style_bg_color(c,UI_CARD,0); lv_obj_set_style_bg_opa(c,LV_OPA_90,0);
    lv_obj_set_style_border_color(c,UI_CARD_HI,0); lv_obj_set_style_border_width(c,2,0);
    lv_obj_set_style_radius(c,24,0); lv_obj_set_style_pad_all(c,24,0);
    lv_obj_remove_flag(c,LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return c;
}
static void standby_create(void)
{
    int w=lv_display_get_horizontal_resolution(NULL), h=lv_display_get_vertical_resolution(NULL);
    bool portrait=h>w; int pad=28,gap=20,top=64,bottom=60;
    int width=w-2*pad, height=h-top-bottom;
    int cw=portrait ? width : (width-gap)*55/100;
    int ch=portrait ? height*42/100 : height;
    int rx=portrait ? pad : pad+cw+gap, ry=portrait ? top+ch+gap : top;
    int rw=portrait ? width : width-cw-gap, rh=portrait ? height-ch-gap : height;
    int status_h=160, cal_h=rh-status_h-gap;
    lv_obj_t *brand=standby_label(s_scr,"CLIENTRE 5  /  STANDBY",FONT_SMALL,UI_MUTED);
    lv_obj_set_pos(brand,pad,22);
    lv_obj_t *card=standby_card(pad,top,cw,ch);
    lv_obj_t *l=standby_label(card,"LOCAL TIME",FONT_SMALL,UI_MUTED); lv_obj_align(l,LV_ALIGN_TOP_LEFT,0,0);
    s_clock=standby_label(card,"00:00",&font_clock192,UI_ACCENT);
    lv_obj_align(s_clock,LV_ALIGN_CENTER,0,-12);
    s_date=standby_label(card,"",FONT_BODY,UI_TEXT); lv_obj_align(s_date,LV_ALIGN_BOTTOM_LEFT,0,-42);
    l=standby_label(card,"24H   /   PIXEL CLOCK",FONT_SMALL,UI_MUTED); lv_obj_align(l,LV_ALIGN_BOTTOM_LEFT,0,0);
    card=standby_card(rx,ry,rw,cal_h);
    s_month=standby_label(card,"",FONT_TITLE,UI_ACCENT);
    lv_obj_t *grid=lv_obj_create(card); lv_obj_set_pos(grid,0,64);
    int gw=rw-48, gh=cal_h-112, cellw=gw/7, cellh=gh/7;
    lv_obj_set_size(grid,gw,gh); lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid,gw,gh); lv_obj_set_pos(grid,0,64);
    lv_obj_remove_flag(grid,LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    const char *days[]={"SU","MO","TU","WE","TH","FR","SA"};
    for(int i=0;i<7;i++) {
        l=standby_label(grid,days[i],FONT_SMALL,UI_MUTED);
        lv_obj_set_size(l,cellw,cellh); lv_obj_set_pos(l,i*cellw,0);
        lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
    }
    for(int i=0;i<42;i++) {
        l=standby_label(grid,"",FONT_BODY,UI_TEXT); s_days[i]=l;
        lv_obj_set_size(l,cellw-4,cellh-2); lv_obj_set_pos(l,(i%7)*cellw+2,(i/7+1)*cellh);
        lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_bg_color(l,UI_ACCENT,0); lv_obj_set_style_radius(l,6,0);
    }
    int half=(rw-gap)/2;
    card=standby_card(rx,ry+cal_h+gap,half,status_h);
    s_battery=standby_label(card,"",FONT_TITLE,UI_ACCENT);
    s_power=standby_label(card,"",FONT_SMALL,UI_MUTED);
    lv_obj_set_width(s_power,half-48); lv_label_set_long_mode(s_power,LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s_power,0,48);
    s_charge=lv_bar_create(card); lv_obj_set_size(s_charge,half-48,8); lv_obj_align(s_charge,LV_ALIGN_BOTTOM_LEFT,0,0);
    lv_obj_set_style_bg_color(s_charge,UI_CARD_HI,0); lv_obj_set_style_bg_color(s_charge,UI_ACCENT,LV_PART_INDICATOR);
    lv_obj_remove_flag(s_charge,LV_OBJ_FLAG_CLICKABLE);
    card=standby_card(rx+half+gap,ry+cal_h+gap,rw-half-gap,status_h);
    l=standby_label(card,LV_SYMBOL_WIFI,FONT_ICON,UI_ACCENT);
    s_network=standby_label(card,"",FONT_BODY,UI_TEXT); lv_obj_set_pos(s_network,0,42);
    lv_obj_set_width(s_network,rw-half-gap-48); lv_label_set_long_mode(s_network,LV_LABEL_LONG_DOT);
    s_ip=standby_label(card,"",FONT_SMALL,UI_MUTED); lv_obj_set_pos(s_ip,0,78);
    lv_obj_set_width(s_ip,rw-half-gap-48); lv_label_set_long_mode(s_ip,LV_LABEL_LONG_DOT);
    l=standby_label(s_scr,tr(STR_SS_EXIT_HINT),FONT_SMALL,UI_MUTED); lv_obj_align(l,LV_ALIGN_BOTTOM_MID,0,-18);
    s_calendar_key=-1;
}

static void update_widgets(void)
{
    if (!g_settings.ss_classic) { standby_update(); return; }
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    char b[96];
    strftime(b, sizeof(b), "%H:%M", &tm); lv_label_set_text(s_elems[0], b);
    strftime(b, sizeof(b), "%Y-%m-%d %a", &tm); lv_label_set_text(s_elems[1], b);
    int bat = sysinfo_battery_percent();
    if (bat >= 0) snprintf(b, sizeof(b), LV_SYMBOL_BATTERY_FULL " %d%%  %s", bat, sysinfo_power_src_text()); else snprintf(b, sizeof(b), "%s", sysinfo_power_src_text());
    lv_label_set_text(s_elems[2], b);
    char ip[48]; wifi_mgr_get_ip4(ip, sizeof(ip));
    snprintf(b, sizeof(b), wifi_mgr_is_connected() ? LV_SYMBOL_WIFI " %s  %s" : LV_SYMBOL_WIFI " -", wifi_mgr_ssid(), ip);
    lv_label_set_text(s_elems[3], b);
}

static void load_wallpaper(void)
{
    if (!g_settings.ss_wallpaper_dir[0] || !sd_is_mounted()) return;
    DIR *d = opendir(g_settings.ss_wallpaper_dir); if (!d) return;
    char names[64][128]; int n = 0; struct dirent *de;
    while ((de = readdir(d)) && n < 64) { const char *x = fs_ext(de->d_name); if (!strcmp(x, "jpg") || !strcmp(x, "jpeg")) strlcpy(names[n++], de->d_name, 128); }
    closedir(d);
    if (!n) return;
    s_wall_idx = (s_wall_idx + 1) % n;
    char p[300]; snprintf(p, sizeof(p), "%s/%s", g_settings.ss_wallpaper_dir, names[s_wall_idx]);
    lv_image_dsc_t *dsc = image_loader_jpeg(p, lv_display_get_horizontal_resolution(NULL), lv_display_get_vertical_resolution(NULL), NULL, 0);
    if (!dsc) return;
    lv_image_set_src(s_wall, NULL);
    if (s_wall_dsc) image_loader_free(s_wall_dsc);
    s_wall_dsc = dsc;
    lv_image_set_src(s_wall, dsc);
    lv_image_set_inner_align(s_wall, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_remove_flag(s_wall, LV_OBJ_FLAG_HIDDEN);
}

static void tick_cb(lv_timer_t *t) { (void)t; update_widgets(); }
static void wall_cb(lv_timer_t *t) { (void)t; load_wallpaper(); }

static void finish_stop(void *arg)
{
    (void)arg;
    /* Replacing a screen deletes the old one.  Do this after the touch event
     * has unwound; deleting its target from inside LV_EVENT_CLICKED leaves
     * LVGL dispatching an event through an object that no longer exists. */
    ui_nav_refresh();
    lv_display_trigger_activity(NULL);
}

static void saver_deleted(lv_event_t *e)
{
    lv_obj_t *deleted = lv_event_get_target_obj(e);
    if (s_scr == deleted) s_scr = NULL;
    s_wall = NULL;
    s_clock = s_date = s_month = NULL;
    s_battery = s_power = s_network = s_ip = NULL;
    s_charge = NULL;
    if (s_wall_dsc) { image_loader_free(s_wall_dsc); s_wall_dsc = NULL; }
    s_stopping = false;
}

static void stop(void)
{
    if (!s_scr || s_stopping) return;
    s_stopping = true;
    if (s_tick) lv_timer_delete(s_tick); if (s_wall_timer) lv_timer_delete(s_wall_timer);
    s_tick = s_wall_timer = NULL;
    if (s_edit_dirty) { settings_save(); s_edit_dirty = false; }
    /* Report the wake immediately, but retain the object until LVGL is out of
     * the input callback.  saver_deleted owns wallpaper cleanup. */
    s_scr = NULL;
    ui_async(finish_stop, NULL);
}

static void elem_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_LONG_PRESSED) { lv_obj_add_flag(o, LV_OBJ_FLAG_USER_1); lv_obj_set_style_outline_width(o, 2, 0); lv_obj_set_style_outline_color(o, UI_ACCENT, 0); }
    else if (code == LV_EVENT_PRESSING && lv_obj_has_flag(o, LV_OBJ_FLAG_USER_1)) {
        lv_indev_t *in = lv_indev_active(); lv_point_t v; lv_indev_get_vect(in, &v);
        lv_obj_set_pos(o, lv_obj_get_x(o) + v.x, lv_obj_get_y(o) + v.y);
    } else if (code == LV_EVENT_RELEASED) {
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_USER_1)) {
            lv_obj_remove_flag(o, LV_OBJ_FLAG_USER_1); lv_obj_set_style_outline_width(o, 0, 0);
            g_settings.ss_elem_x[i] = lv_obj_get_x(o); g_settings.ss_elem_y[i] = lv_obj_get_y(o); s_edit_dirty = true;
        }
    }
}
static void bg_cb(lv_event_t *e) { (void)e; stop(); }

void ui_screensaver_start(void)
{
    if (s_scr || s_stopping) return;
    softkbd_hide_all();
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    s_wall = lv_image_create(s_scr);
    lv_obj_set_size(s_wall, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_wall, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_scr, bg_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_scr, saver_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_add_flag(s_scr, LV_OBJ_FLAG_CLICKABLE);
    if (!g_settings.ss_classic) standby_create();
    else {
    const lv_font_t *fonts[4] = { FONT_TITLE, FONT_BODY, FONT_BODY, FONT_BODY };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *l = lv_label_create(s_scr);
        lv_obj_set_style_text_font(l, fonts[i], 0);
        lv_obj_set_style_text_color(l, (ui_theme_pixel() && g_settings.ss_elem_color[i] == 0xFFFFFF) ? UI_TEXT : lv_color_hex(g_settings.ss_elem_color[i]), 0);
        lv_obj_set_style_pad_all(l, ui_theme_pad(6), 0);
        lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(l, g_settings.ss_elem_x[i], g_settings.ss_elem_y[i]);
        lv_obj_add_event_cb(l, elem_cb, LV_EVENT_ALL, (void *)(intptr_t)i);
        s_elems[i] = l;
    }
    if (g_settings.ss_elem_x[0] == 40 && g_settings.ss_elem_y[0] == 40) {
        /* default: big clock centred */
        lv_obj_set_style_text_font(s_elems[0], FONT_TITLE, 0);
    }
    }
    update_widgets();
    load_wallpaper();
    s_tick = lv_timer_create(tick_cb, 1000, NULL);
    if (g_settings.ss_wallpaper_dir[0]) s_wall_timer = lv_timer_create(wall_cb, (g_settings.ss_switch_min ? g_settings.ss_switch_min : 1) * 60000, NULL);
    ui_music_bar_update();
    ui_motion_load(s_scr, LV_SCR_LOAD_ANIM_FADE_IN);   /* previous screen is deleted; nav recreates it */
}

bool ui_screensaver_active(void) { return s_scr != NULL; }

/* idle detection */
static void idle_cb(lv_timer_t *t)
{
    (void)t;
    if (!g_settings.ss_enable || s_scr) return;
    if (g_settings.ss_disable_in_ssh && g_ssh_session_active) return;
    uint32_t idle = lv_display_get_inactive_time(NULL);
    if (idle > (uint32_t)g_settings.ss_idle_min * 60000u) ui_screensaver_start();
}
static bool s_idle_started = false;
static void ensure_idle_timer(void) { if (!s_idle_started) { s_idle_started = true; lv_timer_create(idle_cb, 5000, NULL); } }

/* ---- settings screen ---- */
static void classic_cb(lv_event_t *e) { g_settings.ss_classic=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED); settings_save(); ui_nav_refresh(); }
static void en_cb(lv_event_t *e) { g_settings.ss_enable = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED); settings_save(); }
static void ssh_cb(lv_event_t *e) { g_settings.ss_disable_in_ssh = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED); settings_save(); }
static void idle_prompt_cb(const char *v, void *ud) { (void)ud; int m = atoi(v); if (m < 1 || m > 600) { ui_toast("1-600"); return; } g_settings.ss_idle_min = m; settings_save(); ui_nav_refresh(); }
static void idle_row_cb(lv_event_t *e) { (void)e; char b[8]; snprintf(b, 8, "%d", g_settings.ss_idle_min); ui_prompt(tr(STR_SS_IDLE_MIN), "1-600", b, false, idle_prompt_cb, NULL); }
static void sw_prompt_cb(const char *v, void *ud) { (void)ud; int m = atoi(v); if (m < 1 || m > 600) { ui_toast("1-600"); return; } g_settings.ss_switch_min = m; settings_save(); ui_nav_refresh(); }
static void sw_row_cb(lv_event_t *e) { (void)e; char b[8]; snprintf(b, 8, "%d", g_settings.ss_switch_min); ui_prompt(tr(STR_SS_SWITCH_MIN), "1-600", b, false, sw_prompt_cb, NULL); }
static void dir_prompt_cb(const char *v, void *ud)
{
    (void)ud;
    if (v[0] && v[0] != '/') { ui_toast(tr(STR_SS_SELECT_FOLDER)); return; }
    strlcpy(g_settings.ss_wallpaper_dir, v, sizeof(g_settings.ss_wallpaper_dir)); settings_save(); ui_nav_refresh();
}
static void dir_row_cb(lv_event_t *e) { (void)e; ui_prompt(tr(STR_SS_WALLPAPER_DIR), "/sdcard/Wallpapers", g_settings.ss_wallpaper_dir, false, dir_prompt_cb, NULL); }
static void reset_cb(lv_event_t *e)
{
    (void)e;
    int16_t x[4] = { 40, 40, 40, 40 }, y[4] = { 40, 160, 220, 270 };
    for (int i = 0; i < 4; i++) { g_settings.ss_elem_x[i] = x[i]; g_settings.ss_elem_y[i] = y[i]; g_settings.ss_elem_color[i] = 0xFFFFFF; }
    settings_save(); ui_toast(tr(STR_SS_SAVED));
}
static void start_cb(lv_event_t *e) { (void)e; ui_screensaver_start(); }
static void color_menu_cb(int i, void *ud)
{
    static const uint32_t cols[] = { 0xFFFFFF, 0xFDE68A, 0x93C5FD, 0x86EFAC, 0xFCA5A5, 0xC4B5FD };
    int idx = (int)(intptr_t)ud;
    if (i < 0) return;
    g_settings.ss_elem_color[idx] = cols[i]; settings_save(); ui_toast(tr(STR_SS_SAVED));
}
static void color_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const char *items[] = { "White", "Yellow", "Blue", "Green", "Red", "Purple" };
    ui_menu(tr(STR_SS_MOVE_HINT), items, 6, color_menu_cb, (void *)(intptr_t)idx);
}

static void position_prompt_cb(const char *v, void *ud)
{
    int idx = (int)(intptr_t)ud, x, y;
    char extra;
    int w = lv_display_get_horizontal_resolution(NULL), h = lv_display_get_vertical_resolution(NULL);
    if (sscanf(v, "%d , %d %c", &x, &y, &extra) != 2 || x < 0 || y < 0 || x >= w || y >= h) {
        ui_toast("X, Y: outside screen"); return;
    }
    g_settings.ss_elem_x[idx] = x; g_settings.ss_elem_y[idx] = y;
    settings_save(); ui_nav_refresh();
}
static void position_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    char value[32];
    snprintf(value, sizeof(value), "%d, %d", g_settings.ss_elem_x[idx], g_settings.ss_elem_y[idx]);
    ui_prompt("X, Y", "40, 160", value, false, position_prompt_cb, (void *)(intptr_t)idx);
}

lv_obj_t *ui_screensaver_settings_create(void *arg)
{
    (void)arg;
    ensure_idle_timer();
    lv_obj_t *scr = ui_screen_base();
    ui_topbar(scr, tr(STR_SET_SCREENSAVER), true);
    lv_obj_t *c = ui_content(scr);
    char b[32];
    ui_switch_row(c, tr(STR_SS_ENABLE), g_settings.ss_enable, en_cb, NULL);
    snprintf(b, sizeof(b), "%d", g_settings.ss_idle_min); ui_setting_row(c, tr(STR_SS_IDLE_MIN), b, idle_row_cb, NULL);
    ui_switch_row(c, tr(STR_SS_DISABLE_IN_SSH), g_settings.ss_disable_in_ssh, ssh_cb, NULL);
    ui_setting_row(c, tr(STR_SS_WALLPAPER_DIR), g_settings.ss_wallpaper_dir[0] ? g_settings.ss_wallpaper_dir : tr(STR_NONE), dir_row_cb, NULL);
    snprintf(b, sizeof(b), "%d", g_settings.ss_switch_min); ui_setting_row(c, tr(STR_SS_SWITCH_MIN), b, sw_row_cb, NULL);
    ui_switch_row(c,tr(STR_SS_CLASSIC),g_settings.ss_classic,classic_cb,NULL);
    if (g_settings.ss_classic) {
    ui_label(c, tr(STR_SS_MOVE_HINT), FONT_SMALL, UI_MUTED);
    lv_obj_t *row = ui_row(c);
    const char *names[4] = { "Clock", "Date", "Battery", "Network" };
    for (int i = 0; i < 4; i++) ui_button_colored(row, names[i], lv_color_hex(g_settings.ss_elem_color[i] == 0xFFFFFF ? 0x475569 : g_settings.ss_elem_color[i]), color_cb, (void *)(intptr_t)i);
    for (int i = 0; i < 4; i++) {
        char label[32]; snprintf(label, sizeof(label), "%s X, Y", names[i]);
        snprintf(b, sizeof(b), "%d, %d", g_settings.ss_elem_x[i], g_settings.ss_elem_y[i]);
        ui_setting_row(c, label, b, position_cb, (void *)(intptr_t)i);
    }
    }
    lv_obj_t *row2 = ui_row(c);
    ui_button(row2, tr(STR_SS_START_NOW), start_cb, NULL);
    ui_button_colored(row2, tr(STR_SS_LAYOUT_RESET), UI_CARD_HI, reset_cb, NULL);
    return scr;
}

/* Called from app start to arm the idle timer even if the settings page is never opened. */
__attribute__((constructor)) static void ss_ctor(void) {}
void ui_screensaver_arm(void) { ensure_idle_timer(); }
