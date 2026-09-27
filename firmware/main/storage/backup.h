#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BACKUP_DIR SD_MOUNT "/backup"
#define BACKUP_PLAIN BACKUP_DIR "/tab5_backup.json"
#define BACKUP_ENC   BACKUP_DIR "/tab5_backup.enc"
bool backup_export(const char *password, char *out_path, size_t cap, char *err, size_t err_cap);  /* password NULL = plain */
bool backup_import(const char *password, char *err, size_t err_cap);  /* looks for .enc then .json */
bool backup_find(bool *encrypted);
#ifdef __cplusplus
}
#endif
