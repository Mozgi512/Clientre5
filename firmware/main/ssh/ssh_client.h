#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { SSH_ST_IDLE, SSH_ST_CONNECTING, SSH_ST_HOSTKEY, SSH_ST_AUTH, SSH_ST_CONNECTED, SSH_ST_CLOSED, SSH_ST_ERROR } ssh_state_t;

typedef struct {
    char host[128];
    int port;
    char user[64];
    char pass[128];
    char keypath[160];
    bool use_key;
    int cols, rows;
} ssh_params_t;

typedef struct ssh_client ssh_client_t;
typedef void (*ssh_data_cb_t)(const uint8_t *data, size_t len, void *user);
typedef void (*ssh_state_cb_t)(ssh_state_t st, const char *msg, void *user);
/* Called from the SSH task when the host key is unknown/changed. Must block
 * until the user answers; return true to trust. */
typedef bool (*ssh_hostkey_cb_t)(const char *host, int port, const char *fingerprint, bool changed, void *user);

ssh_client_t *ssh_client_start(const ssh_params_t *p, ssh_data_cb_t data_cb, ssh_state_cb_t state_cb,
                               ssh_hostkey_cb_t hostkey_cb, void *user);
void ssh_client_send(ssh_client_t *c, const uint8_t *data, size_t len);
void ssh_client_resize(ssh_client_t *c, int cols, int rows);
void ssh_client_stop(ssh_client_t *c);        /* async; object freed by the task */
ssh_state_t ssh_client_state(ssh_client_t *c);
const char *ssh_client_error(ssh_client_t *c);
/* known hosts (NVS) */
void ssh_known_hosts_clear(void);

#ifdef __cplusplus
}
#endif
