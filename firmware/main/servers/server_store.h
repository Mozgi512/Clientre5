#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SERVER_MAX 24
typedef struct {
    char name[48];
    char host[128];
    int  port;
    char user[64];
    char pass[128];      /* plain in RAM, encrypted in NVS */
    char keypath[160];
    bool use_key;
} server_t;

void server_store_init(void);
void server_store_reload(void);          /* re-read after the TF card came back */
/* Moves the list between NVS and the TF card. False when the card is not usable
 * and nothing was changed. */
bool server_store_set_on_sd(bool on);
int  server_store_count(void);
const server_t *server_store_get(int i);
int  server_store_add(const server_t *s);       /* returns index */
bool server_store_update(int i, const server_t *s);
bool server_store_remove(int i);
void server_store_move(int from, int to);
char *server_store_export_json(bool plain);      /* caller frees */
bool  server_store_import_json(const char *json);
#ifdef __cplusplus
}
#endif
