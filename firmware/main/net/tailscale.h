#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Tailscale membership via the vendored MicroLink client. */
typedef enum { TS_OFF = 0, TS_WAIT_WIFI, TS_CONNECTING, TS_REGISTERING, TS_CONNECTED, TS_RECONNECTING, TS_ERROR } ts_state_t;
typedef struct { char hostname[64]; char ip[16]; bool online, direct, exit_node; uint16_t derp_region; } ts_peer_t;
uint16_t tailscale_home_derp(void);
typedef void (*ts_event_cb_t)(void *user);

void tailscale_init(void);                 /* reads settings; starts if enabled and WiFi is up */
bool tailscale_start(void);               /* needs auth key (first time) and WiFi */
void tailscale_stop(void);
void tailscale_on_wifi(bool connected);   /* called from the WiFi manager hook */
ts_state_t tailscale_state(void);
const char *tailscale_state_text(void);
const char *tailscale_vpn_ip(void);       /* "" if not connected */
int  tailscale_peer_count(void);
bool tailscale_peer(int i, ts_peer_t *out);
/* True if ip is a tailnet address (100.64.0.0/10). */
bool tailscale_is_tailnet_ip(const char *ip);
/* Wake the WireGuard session to a peer (handshake + DISCO) and wait until it is up.
 * Must be called before opening a plain TCP socket to a tailnet address. */
bool tailscale_prepare_peer(const char *ip, uint32_t timeout_ms);
/* Diagnostics: log lwIP netifs/route for ip and try a raw TCP connect (timeout ms). */
void tailscale_tcp_probe(const char *ip, int port, int timeout_ms);
/* MagicDNS-style lookup of a tailnet hostname; returns false if unknown. */
bool tailscale_resolve(const char *host, char *ip_out, size_t cap);
void tailscale_set_event_cb(ts_event_cb_t cb, void *user);
bool tailscale_forget_node(void);         /* factory reset of node keys (re-auth needed) */
const char *tailscale_last_error(void);
#ifdef __cplusplus
}
#endif
