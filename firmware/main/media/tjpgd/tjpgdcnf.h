/* TJpgDec configuration for the application image loader (separate from LVGL's copy). */
#define JD_SZBUF        1024
#define JD_FORMAT       1       /* RGB565 output */
#define JD_USE_SCALE    1       /* enable 1/2, 1/4, 1/8 output scaling */
#define JD_TBLCLIP      1
#define JD_FASTDECODE   1
/* Avoid clashing with LVGL's tjpgd symbols. */
#define jd_prepare      app_jd_prepare
#define jd_decomp       app_jd_decomp
#define jd_restart      app_jd_restart
#define jd_mcu_load     app_jd_mcu_load
#define jd_mcu_output   app_jd_mcu_output
