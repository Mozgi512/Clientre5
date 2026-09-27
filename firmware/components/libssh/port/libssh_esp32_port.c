/* Glue for using the LibSSH-ESP32 sources from a pure ESP-IDF project. */
#include "libssh_esp32_config.h"
#include "libssh/libssh.h"
#include "libssh/callbacks.h"

int libssh_esp32_init(void)
{
    /* libssh must be told about our threading model before any session is
     * created; the noop callbacks are fine because every session is only
     * touched from a single FreeRTOS task. */
    ssh_threads_set_callbacks(ssh_threads_get_noop());
    return ssh_init();
}
