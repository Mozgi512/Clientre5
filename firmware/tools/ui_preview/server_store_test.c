/* Host regression test: real store/JSON logic, simulated NVS/SD persistence. */
#include "servers/server_store.h"
#include "settings/settings.h"
#include "storage/sd.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
app_settings_t g_settings;
static char *nvs_data, *sd_data;
static bool fail_write, card_present=true;
void settings_save(void) {}
bool nvs_set_str_ns(const char *ns,const char *key,const char *value) {
    (void)ns;(void)key;
    if(fail_write) return false;
    free(nvs_data); nvs_data=strdup(value); return true;
}
bool nvs_get_str_alloc(const char *ns,const char *key,char **out) {
    (void)ns;(void)key;
    *out=nvs_data ? strdup(nvs_data) : NULL; return *out!=NULL;
}
bool nvs_erase_key_ns(const char *ns,const char *key) {
    (void)ns;(void)key; free(nvs_data); nvs_data=NULL; return true;
}
char *secure_encrypt_str(const char *s) { return strdup(s); }
char *secure_decrypt_str(const char *s) { return strdup(s); }
bool sd_is_mounted(void) { return card_present; }
bool fs_mkdir_p(const char *p) { (void)p; return card_present; }
bool fs_write_all(const char *p,const void *data,size_t len) {
    (void)p; if(fail_write || !card_present) return false;
    free(sd_data); sd_data=strndup(data,len); return true;
}
bool fs_read_all(const char *p,char **out,size_t *len,size_t max) {
    (void)p;(void)max; if(!card_present || !sd_data) return false;
    *out=strdup(sd_data); *len=strlen(*out); return true;
}
static void check_store(bool sd) {
    free(nvs_data);nvs_data=NULL;free(sd_data);sd_data=NULL;
    g_settings.creds_on_sd=sd;
    server_store_init();
    server_t a={.name="first",.host="one.example",.user="tester",.pass="test-only",.port=22};
    assert(server_store_add(&a)==0);
    server_store_init(); /* simulate reboot, discarding the in-memory list */
    assert(server_store_count()==1);
    assert(!strcmp(server_store_get(0)->host,a.host));
    assert(!strcmp(server_store_get(0)->pass,a.pass));
    strcpy(a.host,"two.example");assert(server_store_add(&a)==1);
    server_store_init();assert(server_store_count()==2);
    assert(!strcmp(server_store_get(1)->host,"two.example"));
    fail_write=true;
    assert(server_store_add(&a)==-1 && server_store_count()==2);
    strcpy(a.host,"unsaved.example");
    assert(!server_store_update(0,&a));
    assert(!strcmp(server_store_get(0)->host,"one.example"));
    assert(!server_store_remove(0) && server_store_count()==2);
    fail_write=false;
    server_store_init();assert(server_store_count()==2);
    assert(server_store_update(0,&a));
    server_store_init();assert(!strcmp(server_store_get(0)->host,"unsaved.example"));
    assert(server_store_remove(1));server_store_init();assert(server_store_count()==1);
}
int main(void) {
    check_store(false);check_store(true);
    free(nvs_data);free(sd_data);
    puts("NVS/SD: first and second additions survive reload; failed writes roll back; update/delete persist.");
}
