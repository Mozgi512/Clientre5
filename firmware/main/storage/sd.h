#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SD_MOUNT "/sdcard"
#define USB_MOUNT "/usb"
bool sd_mount(void);
void sd_unmount(void);
void sd_unmount_keep_card(void);   /* detach FATFS but keep the SDMMC card initialised */
void sd_remount_after_msc(void);
bool sd_is_mounted(void);
bool sd_space(const char *path, uint64_t *total, uint64_t *free_bytes);
bool sd_card_info(char *buf, size_t len);   /* name/type/size */
bool fs_exists(const char *path);
bool fs_is_dir(const char *path);
bool fs_mkdir_p(const char *path);
int  fs_copy_file(const char *src, const char *dst, void (*progress)(uint64_t done, uint64_t total, void *ud), void *ud);
int  fs_remove_recursive(const char *path);
int  fs_copy_recursive(const char *src, const char *dst, void (*progress)(uint64_t, uint64_t, void *), void *ud);
uint64_t fs_size_recursive(const char *path, uint32_t *count);
bool fs_read_all(const char *path, char **out, size_t *len, size_t max);   /* malloc'd, NUL terminated */
bool fs_write_all(const char *path, const void *data, size_t len);
void fs_unique_name(const char *dir, const char *name, char *out, size_t out_len);
const char *fs_ext(const char *name);       /* lowercase extension without dot, "" if none */
#ifdef __cplusplus
}
#endif
