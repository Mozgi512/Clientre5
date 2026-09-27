# libssh 0.11.x (LGPL-2.1) ESP32 port, based on ewpa/LibSSH-ESP32 5.9.0.
# Built here as a plain ESP-IDF C component (no Arduino dependency).
file(GLOB LIBSSH_SRCS
    "src/*.c"
    "src/threads/noop.c"
    "src/threads/mbedtls.c"
    "src/external/*.c")
idf_component_register(
    SRCS ${LIBSSH_SRCS} "port/libssh_esp32_port.c"
    INCLUDE_DIRS "src" "port"
    PRIV_REQUIRES mbedtls lwip newlib esp_netif pthread vfs)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ESP32=1 ESP_PLATFORM=1 LIBSSH_STATIC=1
    LIBSSH_ESP32_COMPAT_HOSTNAME="tab5" LIBSSH_ESP32_COMPAT_HOMEDIR="/sdcard")
target_compile_definitions(${COMPONENT_LIB} PUBLIC LIBSSH_STATIC=1 ESP32=1)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=format -Wno-error=char-subscripts -Wno-format -Wno-char-subscripts
    -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable
    -Wno-implicit-fallthrough -Wno-maybe-uninitialized -Wno-error=deprecated-declarations
    -Wno-deprecated-declarations -Wno-error=incompatible-pointer-types -Wno-strict-aliasing)
