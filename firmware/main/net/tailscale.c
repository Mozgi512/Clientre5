#include "tailscale.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "microlink.h"
#include "microlink_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip.h"
#include <errno.h>
#include "settings/settings.h"
#include "net/wifi_mgr.h"
#include "storage/sd.h"

static const char *TAG = "tailscale";
static microlink_t *s_ml = NULL;
static ts_state_t s_state = TS_OFF;
static ts_event_cb_t s_cb; static void *s_cb_user;
static char s_ip[16] = "";
static char s_err[96] = "";
static bool s_started = false;

static void notify(void) { if (s_cb) s_cb(s_cb_user); }

static void on_state(microlink_t *ml, microlink_state_t st, void *user)
{
    (void)user;
    switch (st) {
    case ML_STATE_IDLE: s_state = TS_OFF; break;
    case ML_STATE_WIFI_WAIT: s_state = TS_WAIT_WIFI; break;
    case ML_STATE_CONNECTING: s_state = TS_CONNECTING; break;
    case ML_STATE_REGISTERING: s_state = TS_REGISTERING; break;
    case ML_STATE_CONNECTED: s_state = TS_CONNECTED; microlink_ip_to_str(microlink_get_vpn_ip(ml), s_ip); ESP_LOGI(TAG, "connected, VPN IP %s", s_ip); break;
    case ML_STATE_RECONNECTING: s_state = TS_RECONNECTING; break;
    default: s_state = TS_ERROR; strlcpy(s_err, "MicroLink error", sizeof(s_err)); break;
    }
    if (st != ML_STATE_CONNECTED) s_ip[0] = 0;
    notify();
}
static void on_peer(microlink_t *ml, const microlink_peer_info_t *p, void *user) { (void)ml; (void)p; (void)user; notify(); }

/* Auth key: settings first, then /sdcard/tailscale/authkey.txt (first line). */
static bool load_auth_key(char *out, size_t cap)
{
    if (g_settings.ts_auth_key[0]) { strlcpy(out, g_settings.ts_auth_key, cap); return true; }
    char *txt = NULL; size_t len = 0;
    if (fs_read_all(SD_MOUNT "/tailscale/authkey.txt", &txt, &len, 4096)) {
        char *nl = strpbrk(txt, "\r\n"); if (nl) *nl = 0;
        strlcpy(out, txt, cap); free(txt);
        if (strncmp(out, "tskey-", 6) == 0) {
            strlcpy(g_settings.ts_auth_key, out, sizeof(g_settings.ts_auth_key)); settings_save();
            return true;
        }
    }
    return false;
}

static char s_key[160], s_name[40], s_ctrl[96];

bool tailscale_start(void)
{
    if (s_started) return true;
    if (!load_auth_key(s_key, sizeof(s_key))) {
        /* A node that already registered can start without a key (keys are kept in NVS by MicroLink). */
        s_key[0] = 0;
    }
    strlcpy(s_name, g_settings.ts_device_name[0] ? g_settings.ts_device_name : "clientre5", sizeof(s_name));
    strlcpy(s_ctrl, g_settings.ts_ctrl_host, sizeof(s_ctrl));
    microlink_config_t cfg = {
        .auth_key = s_key[0] ? s_key : NULL,
        .device_name = s_name,
        .enable_derp = true,
        .enable_stun = true,
        .enable_disco = g_settings.ts_direct ? true : false,   /* false = relay-only (reliable), true = try direct UDP */
        .max_peers = 16,
        .netcheck_override_enabled = true,      /* start on the lowest-RTT DERP region */
        .netcheck_override_threshold_ms = 30,
        .ctrl_host = s_ctrl[0] ? s_ctrl : NULL,
    };
    if (!s_ml) {
        s_ml = microlink_init(&cfg);
        if (!s_ml) { strlcpy(s_err, "microlink_init failed", sizeof(s_err)); s_state = TS_ERROR; notify(); return false; }
        microlink_set_state_callback(s_ml, on_state, NULL);
        microlink_set_peer_callback(s_ml, on_peer, NULL);
    }
    esp_err_t e = microlink_start(s_ml);
    if (e != ESP_OK) { snprintf(s_err, sizeof(s_err), "start failed: %s", esp_err_to_name(e)); s_state = TS_ERROR; notify(); return false; }
    s_started = true;
    s_state = wifi_mgr_is_connected() ? TS_CONNECTING : TS_WAIT_WIFI;
    notify();
    return true;
}

void tailscale_stop(void)
{
    if (!s_started) return;
    microlink_stop(s_ml);
    s_started = false; s_state = TS_OFF; s_ip[0] = 0;
    notify();
}

void tailscale_on_wifi(bool connected)
{
    if (!g_settings.ts_enable) return;
    if (connected && !s_started) tailscale_start();
    else if (connected && s_started) microlink_rebind(s_ml);
}

void tailscale_init(void)
{
    if (g_settings.ts_enable && wifi_mgr_is_connected()) tailscale_start();
}

ts_state_t tailscale_state(void) { return s_state; }
const char *tailscale_state_text(void)
{
    static const char *n[] = { "Off", "Waiting for WiFi", "Connecting", "Registering", "Connected", "Reconnecting", "Error" };
    return n[s_state < 7 ? s_state : 6];
}
const char *tailscale_vpn_ip(void) { return s_ip; }
int tailscale_peer_count(void) { return s_ml ? microlink_get_peer_count(s_ml) : 0; }
bool tailscale_peer(int i, ts_peer_t *out)
{
    microlink_peer_info_t p;
    if (!s_ml || microlink_get_peer_info(s_ml, i, &p) != ESP_OK) return false;
    strlcpy(out->hostname, p.hostname, sizeof(out->hostname));
    microlink_ip_to_str(p.vpn_ip, out->ip);
    out->online = p.online; out->direct = p.direct_path; out->exit_node = p.is_exit_node; out->derp_region = p.derp_region;
    return true;
}
bool tailscale_resolve(const char *host, char *ip_out, size_t cap)
{
    if (!s_ml || s_state != TS_CONNECTED) return false;
    uint32_t ip = microlink_resolve(s_ml, host);
    if (!ip) return false;
    char b[16]; microlink_ip_to_str(ip, b); strlcpy(ip_out, b, cap);
    return true;
}
bool tailscale_is_tailnet_ip(const char *ip)
{
    uint32_t v = microlink_parse_ip(ip);
    return v && (v & 0xFFC00000u) == 0x64400000u;   /* 100.64.0.0/10 */
}

bool tailscale_prepare_peer(const char *ip, uint32_t timeout_ms)
{
    if (!s_ml || s_state != TS_CONNECTED) return false;
    uint32_t dest = microlink_parse_ip(ip);
    if (!dest) return false;
    /* MicroLink keeps a single DERP connection (its own home region). Tailscale
     * relays through the *peer's* home region, so for a peer without a direct
     * path we move our DERP connection to that region first. */
    int n = microlink_get_peer_count(s_ml);
    for (int i = 0; i < n; i++) {
        microlink_peer_info_t p;
        if (microlink_get_peer_info(s_ml, i, &p) != ESP_OK || p.vpn_ip != dest) continue;
        ESP_LOGI(TAG, "peer %s: online=%d direct=%d home DERP %u (ours %u, advertised %u)", p.hostname, p.online, p.direct_path, p.derp_region, s_ml->derp_home_region, s_ml->derp_region_default);
        if (!p.direct_path && p.derp_region > 0 && p.derp_region != s_ml->derp_home_region) {
            ESP_LOGW(TAG, "peer %s home DERP %u != ours %u: switching DERP region", p.hostname, p.derp_region, s_ml->derp_home_region);
            s_ml->derp_region_default = p.derp_region;
            s_ml->derp_home_region = p.derp_region;
            /* Reconnect DERP *and* re-register with the control plane so the
             * new PreferredDERP reaches the peers immediately; they relay to
             * whatever region we advertise. */
            microlink_rebind(s_ml);
            EventBits_t bits = xEventGroupWaitBits(s_ml->events, ML_EVT_DERP_CONNECTED | ML_EVT_COORD_REGISTERED, pdFALSE, pdTRUE, pdMS_TO_TICKS(20000));
            ESP_LOGI(TAG, "DERP region %u: derp %s, coord %s", p.derp_region,
                     (bits & ML_EVT_DERP_CONNECTED) ? "connected" : "NOT connected", (bits & ML_EVT_COORD_REGISTERED) ? "registered" : "NOT registered");
            vTaskDelay(pdMS_TO_TICKS(2000));   /* let the peer pick up the new map */
        }
        break;
    }
    ml_wg_mgr_trigger_handshake(s_ml, dest);
    ml_wg_mgr_send_cmm(s_ml, dest);
    uint32_t waited = 0;
    bool up = ml_wg_mgr_peer_is_up(s_ml, dest);
    while (!up && waited < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(500));
        waited += 500;
        up = ml_wg_mgr_peer_is_up(s_ml, dest);
        if (!up && (waited % 5000) == 0) { ml_wg_mgr_trigger_handshake(s_ml, dest); ml_wg_mgr_send_cmm(s_ml, dest); }
    }
    ESP_LOGI(TAG, "peer %s tunnel %s after %lu ms", ip, up ? "up" : "NOT up", (unsigned long)waited);
    return up;
}

void tailscale_tcp_probe(const char *ip, int port, int timeout_ms)
{
    ip4_addr_t dst; ip4addr_aton(ip, &dst);
    for (struct netif *n = netif_list; n; n = n->next) {
        ESP_LOGI(TAG, "netif %c%c%d up=%d link=%d ip=%s mask=%s mtu=%d", n->name[0], n->name[1], n->num,
                 netif_is_up(n), netif_is_link_up(n), ip4addr_ntoa(netif_ip4_addr(n)), ip4addr_ntoa(netif_ip4_netmask(n)), n->mtu);
    }
    struct netif *r = ip4_route(&dst);
    ESP_LOGI(TAG, "route %s -> %s", ip, r ? (char[]){ r->name[0], r->name[1], (char)('0' + r->num), 0 } : "(none)");
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) { ESP_LOGE(TAG, "probe socket failed errno=%d", errno); return; }
    int fl = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(port) }; sa.sin_addr.s_addr = dst.addr;
    int rc = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
    if (rc < 0 && errno != EINPROGRESS) { ESP_LOGE(TAG, "probe connect failed errno=%d", errno); close(fd); return; }
    fd_set wf; FD_ZERO(&wf); FD_SET(fd, &wf);
    struct timeval tv = { .tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000 };
    rc = select(fd + 1, NULL, &wf, NULL, &tv);
    int err = 0; socklen_t el = sizeof(err); getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
    if (rc > 0 && err == 0) {
        char banner[96] = { 0 };
        struct timeval rt = { .tv_sec = 5 }; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof(rt));
        fcntl(fd, F_SETFL, fl);
        int n = recv(fd, banner, sizeof(banner) - 1, 0);
        ESP_LOGI(TAG, "probe %s:%d TCP CONNECTED; banner(%d)=%s", ip, port, n, n > 0 ? banner : "(none)");
    } else ESP_LOGE(TAG, "probe %s:%d TCP FAILED select=%d so_error=%d", ip, port, rc, err);
    close(fd);
}

void tailscale_set_event_cb(ts_event_cb_t cb, void *user) { s_cb = cb; s_cb_user = user; }
bool tailscale_forget_node(void)
{
    tailscale_stop();
    if (s_ml) { microlink_destroy(s_ml); s_ml = NULL; }
    return microlink_factory_reset() == ESP_OK;
}
const char *tailscale_last_error(void) { return s_err; }
uint16_t tailscale_home_derp(void) { return s_ml ? s_ml->derp_home_region : 0; }
