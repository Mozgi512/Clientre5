file(GLOB_RECURSE APP_SRCS
    "*.c")
list(FILTER APP_SRCS EXCLUDE REGEX ".*/fonts/font_.*_unused\\.c$")
idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS "."
    EMBED_FILES "ime/dict/skk_m.bin" "ime/dict/pinyin.bin"
    EMBED_TXTFILES "web/webfm.html"
    PRIV_REQUIRES nvs_flash esp_wifi esp_netif esp_event lwip json mbedtls fatfs vfs esp_http_server esp_http_client
                  esp_https_ota app_update esp_partition esp_timer esp_driver_i2c esp_driver_gpio esp_driver_jpeg esp_driver_ppa
                  esp_psram spi_flash esp_app_format sdmmc driver esp_video
                  libssh microlink
    REQUIRES esp_lvgl_port lvgl m5stack_tab5 esp_lcd_touch esp_lcd esp_lcd_touch esp_lcd usb_host_msc usb_host_hid esp_audio_codec esp_tinyusb esp_wifi_remote)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format -Wno-unused-function -Wno-missing-field-initializers -Wno-misleading-indentation -Wno-unused-variable -Wno-unused-but-set-variable -Wno-misleading-indentation -Wno-unused-variable -Wno-unused-but-set-variable)
