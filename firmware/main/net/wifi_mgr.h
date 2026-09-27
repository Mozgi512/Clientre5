#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MAX_SAVED 12
typedef struct { char ssid[33]; char pass[65]; } wifi_saved_t;
typedef struct { char ssid[33]; int8_t rssi; uint8_t auth; uint8_t channel; } wifi_scan_item_t;

typedef enum { WIFI_EV_CONNECTED, WIFI_EV_DISCONNECTED, WIFI_EV_CONNECT_FAILED, WIFI_EV_SCAN_DONE, WIFI_EV_GOT_IP6 } wifi_ev_t;
typedef void (*wifi_ev_cb_t)(wifi_ev_t ev, void *user);

void wifi_mgr_init(void);                  /* start driver, auto-connect to last used */
bool wifi_mgr_is_connected(void);
bool wifi_mgr_is_connecting(void);     /* association in progress */
bool wifi_mgr_is_started(void);
const char *wifi_mgr_ssid(void);
void wifi_mgr_get_ip4(char *buf, size_t len);
void wifi_mgr_get_ip6(char *buf, size_t len);    /* first global/link-local */
int  wifi_mgr_rssi(void);
int  wifi_mgr_channel(void);
void wifi_mgr_connect(const char *ssid, const char *pass, bool save);
void wifi_mgr_disconnect(void);
bool wifi_mgr_scan_start(void);
int  wifi_mgr_scan_results(wifi_scan_item_t *out, int max);
int  wifi_mgr_saved_count(void);
const wifi_saved_t *wifi_mgr_saved(int i);
void wifi_mgr_forget(int i);
void wifi_mgr_set_cb(wifi_ev_cb_t cb, void *user);          /* UI screen slot */
void wifi_mgr_set_global_cb(wifi_ev_cb_t cb, void *user);   /* app-wide slot */
/* Serialization for backup */
char *wifi_mgr_export_json(void);           /* caller frees */
bool wifi_mgr_import_json(const char *json);
const char *wifi_mgr_last_error(void);

#ifdef __cplusplus
}
#endif
