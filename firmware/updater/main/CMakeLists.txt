idf_component_register(SRCS "updater_main.c" INCLUDE_DIRS "." PRIV_REQUIRES fatfs sdmmc esp_driver_sdmmc app_update esp_partition mbedtls json esp_app_format nvs_flash esp_driver_gpio esp_driver_i2c)
