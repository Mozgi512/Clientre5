#include "ui/ui.h"
#include "term/term_view.h"
#include "media/image_loader.h"
#include "media/camera.h"
#include "ui/ui_boot.h"
#include "ui/ui_motion.h"
#include "settings/settings.h"
#include "net/tailscale.h"
#include "media/audio_player.h"
#include "input/keyboard.h"
#include "sys/sysinfo.h"
#include "net/wifi_mgr.h"
#include "ime/ime.h"
#include "servers/server_store.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
app_settings_t g_settings;
void settings_save(void) {}
void settings_reset_defaults(void) { ui_theme_set_preset(0); }
void ui_music_bar_update(void) {}
bool g_ssh_session_active=false;
void softkbd_hide_all(void) {}
int softkbd_height(void) { return 300; }
const char *sysinfo_power_src_text(void) { return "USB-C"; }
const char *wifi_mgr_ssid(void) { return "Clientre Wi-Fi"; }
bool sd_is_mounted(void) { return true; }
const char *fs_ext(const char *p) { (void)p; return ""; }
lv_image_dsc_t *image_loader_jpeg(const char *p,int w,int h,char *s,int cap) { return NULL; }
void image_loader_free(lv_image_dsc_t *p) {}
int sysinfo_battery_percent(void) { return 98; }
const char *sysinfo_app_version(void) { return "preview"; }
void sysinfo_apply_timezone(void) {}
void sysinfo_start_sntp(void) {}
bool wifi_mgr_is_connected(void) { return true; }
bool wifi_mgr_is_connecting(void) { return false; }
void wifi_mgr_get_ip4(char *b, size_t n) { snprintf(b,n,"192.168.1.25"); }
void wifi_mgr_disconnect(void) {}
bool keyboard_present(void) { return true; }
bool keyboard_usb_present(void) { return false; }
static void *focused_textarea;
void keyboard_lvgl_focus(void *p) { focused_textarea = p; }
void *keyboard_lvgl_focused(void) { return focused_textarea; }
void ime_commit_preedit(void) {}
void ime_reset(void) {}
bool ime_is_composing(void) { return false; }
void keyboard_set_layout(uint8_t layout) { (void)layout; }
void keyboard_set_led(uint8_t a,uint8_t b,uint8_t c,uint8_t d,uint8_t e) {}
ts_state_t tailscale_state(void) { return TS_CONNECTED; }
bool tailscale_start(void) { return true; }
void tailscale_stop(void) {}
void audio_player_toggle(void) {}
ap_state_t audio_player_state(void) { return AP_STOPPED; }
void softkbd_attach_textarea(lv_obj_t *a,lv_obj_t *b) {}
void esp_restart(void) {}
void bsp_display_brightness_set(int v) {}
ime_src_t ime_get_source(void) { return IME_SRC_JA; }
ime_ja_mode_t ime_get_ja_mode(void) { return IME_JA_HIRA; }
void ime_set_source(ime_src_t v) {}
void ime_set_ja_mode(ime_ja_mode_t v) {}
bool ime_load_sd_dict(const char *s) { return false; }
const char *ime_dict_info(void) { return "SKK"; }
bool server_store_set_on_sd(bool on) { return true; }
void server_store_reload(void) {}
int server_store_count(void) { return 3; }
#define SCREEN(name) lv_obj_t *name(void *p) { return ui_screen_base(); }
SCREEN(ui_servers_create) SCREEN(ui_files_create) SCREEN(ui_music_create)
SCREEN(ui_wifi_create) SCREEN(ui_tailscale_create) SCREEN(ui_webfm_create)
SCREEN(ui_usb_create) SCREEN(ui_ota_create) SCREEN(ui_devinfo_create)
SCREEN(ui_server_form_create) SCREEN(ui_lang_create)
SCREEN(ui_backup_create)
bool camera_start(void) { return true; }
void camera_stop(void) {}
camera_state_t camera_state(void) { return CAMERA_STATE_RUNNING; }
const char *camera_error(void) { return ""; }
bool camera_copy_frame(void *dst,size_t cap,uint32_t *w,uint32_t *h,uint32_t *seq) {
    if(cap<1280U*720U*2U) return false;
    uint16_t *pixels=dst;
    for(unsigned y=0;y<720;y++) for(unsigned x=0;x<1280;x++)
        pixels[y*1280+x]=(uint16_t)((((x*31)/1279)<<11)|(((y*63)/719)<<5)|((x+y)&31));
    if(w)*w=1280; if(h)*h=720; if(seq)(*seq)++;
    return true;
}
bool camera_take_photo(void) { return true; }
camera_photo_state_t camera_photo_state(char *path,size_t len) { if(path&&len)*path=0; return CAMERA_PHOTO_IDLE; }
static int snapshot(lv_obj_t *s, const char *path) {
    int w=lv_obj_get_width(s), h=lv_obj_get_height(s);
    lv_draw_buf_t *snap=lv_snapshot_take(s,LV_COLOR_FORMAT_RGB888);
    if(!snap) return 1;
    FILE *f=fopen(path,"wb");
    if (!f) return 1;
    fprintf(f,"P6\n%d %d\n255\n",w,h);
    for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
        uint8_t *p=snap->data+y*snap->header.stride+x*3;
        fputc(p[2],f); fputc(p[1],f); fputc(p[0],f);
    }
    fclose(f); lv_draw_buf_destroy(snap); return 0;
}
static uint64_t flushed_pixels;
static unsigned flushes;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p) {
    flushed_pixels += (uint64_t)lv_area_get_width(a) * lv_area_get_height(a);
    flushes++;
    lv_display_flush_ready(d);
}
static void step(lv_display_t *d, unsigned ms) {
    lv_tick_inc(ms); lv_timer_handler(); lv_refr_now(d);
}
static void motion_check(lv_display_t *d) {
    ui_nav_home(); step(d, 400);
    flushed_pixels=0; flushes=0;
    ui_nav_push(ui_settings_create, NULL);
    lv_refr_now(d);
    uint64_t peak=0;
    const char *capture=getenv("UI_CAPTURE_MOTION");
    if (capture) snapshot(lv_screen_active(), "motion-00.ppm");
    for (unsigned i=0;i<12;i++) {
        uint64_t before=flushed_pixels;
        step(d,33);
        if (flushed_pixels-before>peak) peak=flushed_pixels-before;
        if (capture) {
            char path[32]; snprintf(path,sizeof(path),"motion-%02u.ppm",i+1);
            snapshot(lv_screen_active(),path);
        }
    }
    uint64_t sweep=flushed_pixels;
    printf("EL sweep: %llu pixels, %u flushes, largest animated frame %llu pixels\n",
           (unsigned long long)sweep, flushes, (unsigned long long)peak);
    /* No sweep invalidations survive completion. Hardware status is static. */
    flushed_pixels=0; step(d,400);
    assert(flushed_pixels==0);
    ui_nav_home(); step(d,400);
    flushed_pixels=0; flushes=0;
    lv_obj_t *next=ui_settings_create(NULL);
    lv_screen_load_anim(next, LV_SCR_LOAD_ANIM_MOVE_LEFT, 150, 0, true);
    for(unsigned i=0;i<12;i++) step(d,33);
    printf("Legacy slide, same widgets: %llu pixels, %u flushes\n",
           (unsigned long long)flushed_pixels, flushes);
    assert(sweep < flushed_pixels);
    /* Interrupt a sweep by navigation and by deleting its screen. */
    ui_nav_home(); step(d,33); ui_nav_push(ui_settings_create,NULL); step(d,33);
    ui_theme_set_preset(8); ui_theme_apply(); step(d,400);
    ui_theme_set_preset(0); ui_theme_apply();
    ui_motion_start(lv_screen_active(),NULL,true); step(d,33);
    lv_obj_t *old=lv_screen_active(), *blank=lv_obj_create(NULL);
    lv_screen_load(blank); lv_obj_delete(old); step(d,400);
    puts("Motion lifecycle checks passed.");
}
static int keyboard_clicks, keyboard_menus, keyboard_releases, keyboard_answer;
static void keyboard_test_cb(lv_event_t *e) {
    if (lv_event_get_code(e)==LV_EVENT_CLICKED) keyboard_clicks++;
    if (lv_event_get_code(e)==LV_EVENT_LONG_PRESSED) keyboard_menus++;
    if (lv_event_get_code(e)==LV_EVENT_RELEASED) keyboard_releases++;
}
static void keyboard_confirmed(bool yes, void *ud) { keyboard_answer=yes ? 1 : -1; }
static bool press_key(special_key_t key, uint8_t mods, bool terminal) {
    key_event_t ev={.type=KEY_EV_SPECIAL,.key=key,.mods=mods};
    return ui_keyboard_route(&ev,terminal);
}
static void keyboard_check(void) {
    lv_obj_t *scr=ui_screen_base(); lv_screen_load(scr);
    lv_obj_t *hidden=ui_button(scr,"hidden",keyboard_test_cb,NULL);
    lv_obj_add_flag(hidden,LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *disabled=ui_button(scr,"disabled",keyboard_test_cb,NULL);
    lv_obj_add_state(disabled,LV_STATE_DISABLED);
    lv_obj_t *touch_only=lv_obj_create(scr);
    lv_obj_add_flag(touch_only,UI_KEYBOARD_SKIP);
    ui_button(touch_only,"soft key",keyboard_test_cb,NULL);
    lv_obj_t *button=ui_button(scr,"action",keyboard_test_cb,NULL);
    lv_obj_add_event_cb(button,keyboard_test_cb,LV_EVENT_LONG_PRESSED,NULL);
    lv_obj_t *slider=lv_slider_create(scr); lv_slider_set_range(slider,0,100);
    lv_obj_add_event_cb(slider,keyboard_test_cb,LV_EVENT_RELEASED,NULL);
    lv_obj_t *ta=ui_textarea(scr,"text",false,true);
    press_key(SK_TAB,0,false); assert(lv_obj_has_state(button,LV_STATE_FOCUS_KEY));
    press_key(SK_ENTER,0,false); assert(keyboard_clicks==1);
    press_key(SK_ENTER,MOD_CTRL,false); assert(keyboard_menus==1);
    press_key(SK_TAB,0,false); assert(lv_obj_has_state(slider,LV_STATE_FOCUS_KEY));
    press_key(SK_RIGHT,0,false); assert(lv_slider_get_value(slider)==1 && keyboard_releases==1);
    press_key(SK_TAB,0,false); assert(keyboard_lvgl_focused()==ta);
    assert(!press_key(SK_LEFT,0,false));
    press_key(SK_TAB,MOD_SHIFT,false); assert(keyboard_lvgl_focused()==NULL);
    ui_confirm("test",NULL,keyboard_confirmed,NULL);
    press_key(SK_ESC,0,true); assert(keyboard_answer==-1 && keyboard_clicks==1);
    ui_confirm("test",NULL,keyboard_confirmed,NULL);
    press_key(SK_TAB,0,true); press_key(SK_TAB,0,true); press_key(SK_ENTER,0,true);
    assert(keyboard_answer==1);
    assert(!press_key(SK_TAB,0,true)); /* untouched SSH Tab */
    press_key(SK_TAB,MOD_CTRL,true); assert(ui_keyboard_owns_input());
    press_key(SK_ESC,0,true); assert(!ui_keyboard_owns_input());
    assert(!press_key(SK_ESC,0,true)); /* untouched SSH Esc */
    press_key(SK_TAB,0,false);
    lv_obj_delete(button); /* focused deletion must not leave a dangling target */
    press_key(SK_TAB,0,false); press_key(SK_RIGHT,0,false);
    ui_prompt("test","", "",false,NULL,NULL);
    press_key(SK_TAB,0,true); assert(keyboard_lvgl_focused()!=NULL);
    press_key(SK_ESC,0,true); assert(keyboard_lvgl_focused()==NULL);
    keyboard_lvgl_focus(ta); /* mixed touch/physical input follows the touched field */
    assert(!press_key(SK_RIGHT,0,false)); assert(lv_obj_has_state(ta,LV_STATE_FOCUS_KEY));
    ui_confirm("outer",NULL,keyboard_confirmed,NULL);
    ui_alert("inner","test");
    press_key(SK_ESC,0,true); assert(ui_keyboard_owns_input());
    press_key(SK_ESC,0,true); assert(!ui_keyboard_owns_input());
    lv_obj_t *sw=ui_switch_row(scr,"switch",false,NULL,NULL);
    keyboard_lvgl_focus(ta);
    press_key(SK_RIGHT,0,false); press_key(SK_TAB,0,false);
    lv_obj_t *toggle=lv_obj_get_user_data(sw);
    assert(lv_obj_has_state(toggle,LV_STATE_FOCUS_KEY));
    press_key(SK_ENTER,0,false); assert(lv_obj_has_state(toggle,LV_STATE_CHECKED));
    press_key(SK_ENTER,0,false); assert(!lv_obj_has_state(toggle,LV_STATE_CHECKED));
    /* Exercise the real home tiles, especially the second row without quick actions. */
    for (unsigned tile=0; tile<11; tile++) {
        ui_nav_home();
        lv_obj_t *home=lv_screen_active();
        lv_obj_t *tiles=lv_obj_get_child(home,1);
        lv_obj_t *target=lv_obj_get_child(tiles,tile);
        for (unsigned n=0;n<12 && !lv_obj_has_state(target,LV_STATE_FOCUS_KEY);n++)
            press_key(SK_TAB,0,false);
        assert(lv_obj_has_state(target,LV_STATE_FOCUS_KEY));
        press_key(SK_ENTER,0,false);
        assert(lv_screen_active()!=home);
    }
    puts("Keyboard navigation, slider persistence, modal isolation, mixed input, switches, deletion and SSH routing checks passed.");
}

static void terminal_spacing_check(void) {
    lv_obj_t *scr=ui_screen_base(); lv_screen_load(scr);
    fonts_set_term_line_height(32);
    vt_t *vt=vt_create(80,16,10);
    term_view_t *tv=term_view_create(scr,vt,&g_term_font);
    lv_obj_set_size(term_view_obj(tv),960,512);
    int cols,rows;
    term_view_fit(tv,&cols,&rows);
    assert(cols==80 && rows==16 && term_view_cell_h(tv)==32);
    const char *sample="ABCDEFGHIJKLMNOPQRSTUVWXYZ gjpqy 0123456789 日本語テスト\r\n";
    for(int i=0;i<12;i++) vt_feed(vt,(const uint8_t *)sample,strlen(sample));
    const char *hide="\033[?25l";
    vt_feed(vt,(const uint8_t *)hide,strlen(hide));
    lv_draw_buf_t *snap=lv_snapshot_take(term_view_obj(tv),LV_COLOR_FORMAT_RGB888);
    assert(snap);
    /* At least four wholly blank scanlines between every pair of rendered rows. */
    for(int row=0;row<11;row++) {
        int last=-1,first=32;
        for(int y=0;y<64;y++) {
            bool ink=false;
            for(int x=0;x<960;x++) {
                uint8_t *p=snap->data+(row*32+y)*snap->header.stride+x*3;
                if(p[0]||p[1]||p[2]) { ink=true; break; }
            }
            if(ink && y<32) last=y;
            if(ink && y>=32 && first==32) first=y-32;
        }
        assert(last>=0 && first<32 && 32+first-last-1>=4);
    }
    lv_draw_buf_destroy(snap);
    assert(snapshot(scr,"/tmp/clientre-terminal-spacing.ppm")==0);
    fonts_set_term_line_height(38);
    term_view_fit(tv,&cols,&rows);
    assert(term_view_cell_h(tv)==38 && rows==13);
    term_view_destroy(tv); vt_destroy(vt);
    puts("Terminal: 32px rows, visible inter-row whitespace and live 38px resize passed.");
}

static lv_point_t test_pointer;
static bool test_pointer_down;
static unsigned test_swipes;
static void pointer_read(lv_indev_t *in, lv_indev_data_t *data) {
    (void)in; data->point=test_pointer;
    data->state=test_pointer_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
static void count_swipe(lv_dir_t dir) { if(dir==LV_DIR_LEFT || dir==LV_DIR_RIGHT) test_swipes++; }
static void pointer_move(lv_indev_t *in,int x,int y,bool down) {
    test_pointer=(lv_point_t){x,y}; test_pointer_down=down;
    lv_tick_inc(20); lv_indev_read(in);
}
static void gesture_check(void) {
    lv_indev_t *in=lv_indev_create(); lv_indev_set_type(in,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in,pointer_read); ui_init();
    lv_obj_t *scr=ui_screen_base(); lv_screen_load(scr);
    lv_obj_remove_flag(scr,LV_OBJ_FLAG_SCROLLABLE);
    ui_screen_set_gesture(scr,count_swipe);
    lv_obj_t *slider=lv_slider_create(scr); lv_obj_set_pos(slider,50,80); lv_obj_set_size(slider,400,40);
    lv_obj_update_layout(scr);
    pointer_move(in,100,100,true);
    for(int x=130;x<=400;x+=30) pointer_move(in,x,100,true);
    pointer_move(in,400,100,false);
    assert(lv_slider_get_value(slider)>50 && test_swipes==0);
    pointer_move(in,100,300,true);
    for(int x=130;x<=400;x+=30) pointer_move(in,x,300,true);
    pointer_move(in,400,300,false);
    assert(test_swipes==1);
    lv_indev_delete(in);
    puts("Touch slider drag changes volume control without track swipe; background swipe retained.");
}
static void terminal_art_check(void) {
    lv_font_glyph_dsc_t glyph;
    assert(lv_font_get_glyph_dsc(&g_term_font,&glyph,0x23f5,0));
    assert(!glyph.is_placeholder && glyph.adv_w==12);
    lv_obj_t *scr=ui_screen_base(); lv_screen_load(scr);
    fonts_set_term_line_height(32);
    vt_t *vt=vt_create(60,12,0);
    term_view_t *tv=term_view_create(scr,vt,&g_term_font);
    lv_obj_set_size(term_view_obj(tv),720,408);
    const char *art="[⏵⏵] ASCII 日本語\r\n  ▐▛███▜▌\r\n ▝▜█████▛▘\r\n   ▘▘ ▝▝\r\n█████\r\n█████\r\n";
    vt_feed(vt,(const uint8_t *)art,strlen(art));
    const char *hide="\033[?25l"; vt_feed(vt,(const uint8_t *)hide,strlen(hide));
    const vt_cell_t *row=vt_view_row(vt,0);
    assert(row[1].cp==0x23f5 && row[2].cp==0x23f5 && row[3].cp==']' && row[5].cp=='A');
    lv_draw_buf_t *snap=lv_snapshot_take(term_view_obj(tv),LV_COLOR_FORMAT_RGB888); assert(snap);
    for(int y=4*32;y<6*32;y++) for(int x=0;x<5*12;x++) {
        uint8_t *p=snap->data+y*snap->header.stride+x*3;
        assert(p[0] || p[1] || p[2]); /* no seams in solid block artwork */
    }
    lv_draw_buf_destroy(snap);
    assert(snapshot(scr,"/tmp/clientre-terminal-art.ppm")==0);
    term_view_destroy(tv); vt_destroy(vt);
    puts("CLI triangles are one cell; full-height block artwork has no gaps across columns or rows.");
}

static void terminal_palette_preview(void) {
    ui_theme_set_terminal_amber();
    assert(g_settings.term_bg==0x241200 && g_settings.term_fg==0xFF9800);
    lv_obj_t *scr=ui_screen_base(); lv_screen_load(scr);
    lv_obj_set_style_bg_color(scr,lv_color_hex(g_settings.term_bg),0);
    vt_set_palette(g_settings.term_ansi); fonts_set_term_line_height(32);
    int columns=(lv_display_get_horizontal_resolution(NULL)-48)/12;
    vt_t *vt=vt_create(columns,20,0);
    term_view_t *tv=term_view_create(scr,vt,&g_term_font);
    lv_obj_set_pos(term_view_obj(tv),24,20); lv_obj_set_size(term_view_obj(tv),columns*12,680);
    term_view_set_colors(tv,lv_color_hex(g_settings.term_fg),lv_color_hex(g_settings.term_bg));
    const char *sample=
        "\033[38;2;215;122;86m ▐▛███▜▌  \033[93mClaude Code\033[37m  CLI color preview\r\n"
        "\033[38;2;215;122;86m▝▜█████▛▘ \033[37mClientre 5\r\n"
        "\033[38;2;215;122;86m  ▘▘ ▝▝   \033[37m/home/user\r\n\r\n"
        "\033[40;97m > hello                                    \033[0m\r\n\r\n"
        "\033[97m* \033[0mHello! What can I help you with today?\r\n\r\n"
        "\033[37m  Ready. 日本語の表示もそのままです。\r\n\r\n\r\n\r\n\r\n"
        "\033[94m                         copied to clipboard\r\n"
        "\033[90m%s\r\n"
        "\033[0m> _\r\n"
        "\033[90m%s\r\n"
        "\033[37m  Status\r\n\033[93m  ⏵⏵ auto mode on\033[37m (CLI status)\033[?25l";
    char rule[1024], output[4096];
    assert(columns*3 < sizeof(rule));
    for(int i=0;i<columns;i++) memcpy(rule+i*3,"─",3);
    rule[columns*3]=0;
    snprintf(output,sizeof(output),sample,rule,rule);
    vt_feed(vt,(const uint8_t *)output,strlen(output));
    assert(vt_view_row(vt,14)[columns-1].cp==0x2500);
    assert(vt_view_row(vt,16)[columns-1].cp==0x2500);
    assert(snapshot(scr,"/tmp/clientre-cli-amber.ppm")==0);
    term_view_destroy(tv); vt_destroy(vt);
    puts("Amber CLI palette preview rendered.");
}

static void font_policy_check(lv_display_t *d) {
    lv_font_glyph_dsc_t latin, han;

    i18n_set_lang(LANG_JA);
    ui_theme_set_preset(0); ui_theme_init(); ui_theme_reinit_lvgl();
    assert(FONT_BODY->dsc == font_px24.dsc);
    assert(g_term_font.dsc == font_px24.dsc);

    i18n_set_lang(LANG_ZH_CN);
    ui_theme_reinit_lvgl();
    assert(FONT_SMALL == FONT_BODY && FONT_BODY == FONT_TITLE);
    assert(FONT_BODY->dsc == font_cjk.dsc);
    assert(g_term_font.dsc == font_cjk.dsc);
    assert(lv_font_get_glyph_dsc(FONT_BODY,&latin,'A',0));
    assert(lv_font_get_glyph_dsc(FONT_BODY,&han,0x7CFB,0)); /* 系 */
    assert(latin.resolved_font == FONT_BODY && han.resolved_font == FONT_BODY);
    assert(lv_font_get_glyph_dsc(&g_term_font,&latin,'A',0));
    assert(lv_font_get_glyph_dsc(&g_term_font,&han,0x7CFB,0));
    assert(latin.resolved_font == &g_term_font && han.resolved_font == &g_term_font);
    ui_nav_home(); step(d,500);
    assert(snapshot(lv_screen_active(),"/tmp/clientre-font-zh-pixel.ppm")==0);

    i18n_set_lang(LANG_EN);
    ui_theme_set_preset(3); ui_theme_init(); ui_theme_reinit_lvgl();
    assert(FONT_BODY->dsc == lv_font_montserrat_22.dsc);
    assert(FONT_TITLE->dsc == lv_font_montserrat_30.dsc);
    assert(g_term_font.dsc == font_cjk.dsc); /* ordinary anti-aliased mono */

    i18n_set_lang(LANG_JA);
    ui_theme_reinit_lvgl();
    assert(FONT_BODY->dsc == font_cjk.dsc && FONT_TITLE->dsc == font_cjk.dsc);
    ui_nav_home(); step(d,500);
    assert(snapshot(lv_screen_active(),"/tmp/clientre-font-ja-modern.ppm")==0);
    puts("Font policy: MaruMinya for retro JA, unified Noto for Chinese, smooth fonts for modern themes passed.");
}

int main(int argc,char **argv) {
    lv_init();
    int w=argc>3?atoi(argv[3]):1280, h=argc>4?atoi(argv[4]):720;
    lv_display_t *d=lv_display_create(w,h);
    void *buf=malloc(w*50*4);
    lv_display_set_buffers(d,buf,NULL,w*50*4,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d,flush);
    g_settings.brightness=58; g_settings.status_items=STATUS_DEFAULT;
    g_settings.term_line_height=26;
    /* Exercise splash before font runtime copies or the application UI exist. */
    if (argc>1 && !strcmp(argv[1],"boot")) {
        ui_theme_set_preset(argc>2?atoi(argv[2]):0);
        ui_boot_show();
        lv_obj_t *boot=lv_screen_active();
        unsigned child_count=lv_obj_get_child_count(boot);
        assert(child_count==1);
        flushed_pixels=0; step(d,1000);
        assert(flushed_pixels==0); /* static logo must not schedule redraws */
        int result=snapshot(boot,"screen.ppm");
        fonts_init(); ui_init(); ui_nav_home(); step(d,400);
        assert(lv_screen_active()!=boot);
        puts("Early boot logo and handoff checks passed.");
        return result;
    }
    fonts_init(); i18n_set_lang(LANG_JA);
    ui_theme_set_preset(argc>2?atoi(argv[2]):0); ui_init();
    if (argc>1 && !strcmp(argv[1],"camera")) {
        ui_nav_push(ui_camera_create,NULL); step(d,500);
        return snapshot(lv_screen_active(),"screen.ppm");
    }
    if (argc>1 && !strcmp(argv[1],"font-policy")) { font_policy_check(d); return 0; }
    if (argc>1 && !strcmp(argv[1],"standby")) {
        lv_indev_t *pointer=lv_indev_create(); lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(pointer,pointer_read); ui_init();
        ui_nav_home(); ui_screensaver_start(); step(d,400);
        assert(ui_screensaver_active());
        assert(snapshot(lv_screen_active(),"/tmp/clientre-standby.ppm")==0);
        key_event_t key={.type=KEY_EV_SPECIAL,.key=SK_ENTER};
        assert(ui_keyboard_route(&key,false)); assert(!ui_screensaver_active());
        step(d,400);
        ui_screensaver_start(); step(d,400);
        lv_obj_t *saver=lv_screen_active();
        lv_obj_t *clock_card=lv_obj_get_child(saver,2);
        lv_obj_t *clock=lv_obj_get_child(clock_card,1);
        lv_area_t clock_area; lv_obj_get_coords(clock,&clock_area);
        int clock_x=(clock_area.x1+clock_area.x2)/2;
        int clock_y=(clock_area.y1+clock_area.y2)/2;
        pointer_move(pointer,clock_x,clock_y,true);
        pointer_move(pointer,clock_x,clock_y,false);
        assert(!ui_screensaver_active());
        step(d,400);
        assert(lv_screen_active()!=saver);
        assert(lv_screen_active()==ui_nav_current());
        assert(ui_nav_is_top(ui_home_create));
        assert(snapshot(lv_screen_active(),"/tmp/clientre-standby-clock-exit.ppm")==0);
        g_settings.ss_classic=1; ui_screensaver_start(); step(d,400);
        assert(ui_screensaver_active());
        lv_obj_send_event(lv_screen_active(),LV_EVENT_CLICKED,NULL);
        assert(!ui_screensaver_active());
        lv_indev_delete(pointer);
        puts("Standby render, keyboard/touch wake, repeated entry and classic mode passed."); return 0;
    }
    if (argc>1 && !strcmp(argv[1],"cli-colors")) { terminal_palette_preview(); return 0; }
    if (argc>1 && !strcmp(argv[1],"gesture")) { gesture_check(); return 0; }
    if (argc>1 && !strcmp(argv[1],"art")) { terminal_art_check(); return 0; }
    if (argc>1 && !strcmp(argv[1],"terminal")) { terminal_spacing_check(); return 0; }
    if (argc>1 && !strcmp(argv[1],"keyboard")) { keyboard_check(); return 0; }
    if (argc>1 && !strcmp(argv[1],"motion")) { motion_check(d); return 0; }
    ui_screen_create_fn create= argc>1 && !strcmp(argv[1],"settings") ? ui_settings_create :
                 argc>1 && !strcmp(argv[1],"appearance") ? ui_appearance_create :
                 argc>1 && !strcmp(argv[1],"camera") ? ui_camera_create : ui_home_create;
    /* Exercise persisted preset indexes and live theme rebuilds before capture. */
    ui_nav_push(create, NULL);
    lv_tick_inc(200); lv_timer_handler();
    for (int i = 0; i < ui_theme_preset_count(); i++) {
        ui_theme_set_preset(i); ui_theme_apply(); lv_tick_inc(200); lv_timer_handler();
    }
    ui_theme_set_preset(argc>2?atoi(argv[2]):0); ui_theme_apply(); lv_tick_inc(200); lv_timer_handler();
    lv_obj_t *s = lv_screen_active();
    if (create == ui_settings_create) {
        /* Real click events, including selection preservation across rebuilds. */
        for (unsigned i = 0; i < 7; i++) {
            lv_obj_t *before = s;
            lv_obj_t *nav = lv_obj_get_child(lv_obj_get_child(s, 1), 0);
            lv_obj_send_event(lv_obj_get_child(nav, i), LV_EVENT_CLICKED, NULL);
            lv_tick_inc(200); lv_timer_handler();
            s = lv_screen_active();
            assert(s == before); /* tab switch must not rebuild the screen */
            lv_obj_t *deck = lv_obj_get_child(lv_obj_get_child(s, 1), 1);
            for (unsigned j = 0; j < 7; j++)
                assert(lv_obj_has_flag(lv_obj_get_child(deck, j), LV_OBJ_FLAG_HIDDEN) == (j != i));
        }
        unsigned category = argc>5 ? (unsigned)atoi(argv[5]) : 0;
        assert(category < 7);
        lv_obj_t *nav = lv_obj_get_child(lv_obj_get_child(s, 1), 0);
        lv_obj_send_event(lv_obj_get_child(nav, category), LV_EVENT_CLICKED, NULL);
        lv_tick_inc(200); lv_timer_handler();
        s = lv_screen_active();
    }
    step(d,400);
    lv_obj_update_layout(s); lv_refr_now(d);
    if (create == ui_home_create) {
        lv_obj_t *content = lv_obj_get_child(s, 1);
        assert(lv_obj_get_child_count(content) == 11);
        for (unsigned i = 0; i < 11; i++) {
            lv_area_t a; lv_obj_get_coords(lv_obj_get_child(content, i), &a);
            assert(a.x1 >= 0 && a.x2 < w && a.y1 >= UI_TOPBAR_H && a.y2 < h);
        }
    }
    return snapshot(s, "screen.ppm");
}
