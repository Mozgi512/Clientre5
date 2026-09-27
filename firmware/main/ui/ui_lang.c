#include "ui.h"
#include "settings/settings.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/m5stack_tab5.h"

static void reboot_timer_cb(lv_timer_t *t)
{
    (void)t;
    bsp_display_backlight_off();
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

static void lang_cb(lv_event_t *e)
{
    lang_t lang = (lang_t)(intptr_t)lv_event_get_user_data(e);
    bool first = (g_settings.lang == LANG_UNSET);
    g_settings.lang = lang;
    settings_save();
    i18n_set_lang(lang);
    ui_theme_reinit_lvgl();
    if (first) {
        ui_nav_home();
    } else {
        ui_alert(tr(STR_SET_LANGUAGE), tr(STR_LANG_CHANGE_NOTE));
        lv_timer_t *t = lv_timer_create(reboot_timer_cb, 1500, NULL);
        lv_timer_set_repeat_count(t, 1);
    }
}

lv_obj_t *ui_lang_create(void *arg)
{
    (void)arg;
    lv_obj_t *scr = ui_screen_base();
    bool first = (g_settings.lang == LANG_UNSET);
    char title[160];
    snprintf(title, sizeof(title), "%s / %s / %s", tr_lang(LANG_EN, STR_SELECT_LANGUAGE),
             tr_lang(LANG_ZH_CN, STR_SELECT_LANGUAGE), tr_lang(LANG_JA, STR_SELECT_LANGUAGE));
    ui_topbar(scr, first ? title : tr(STR_SET_LANGUAGE), !first);
    lv_obj_t *c = ui_content(scr);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i < LANG_COUNT; i++) {
        lv_obj_t *b = ui_button_colored(c, i18n_lang_name((lang_t)i),
                                        (g_settings.lang == i) ? UI_ACCENT : UI_CARD_HI, lang_cb, (void *)(intptr_t)i);
        lv_obj_set_size(b, 360, 64);
    }
    return scr;
}
