#include "sd.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "bsp/m5stack_tab5.h"

static const char *TAG = "sd";
static bool s_mounted = false;

bool sd_mount(void)
{
    if (s_mounted) return true;
    esp_err_t e = bsp_sdcard_mount();
    if (e != ESP_OK) { ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(e)); return false; }
    s_mounted = true;
    sdmmc_card_t *card = bsp_sdcard_get_handle();
    if (card) ESP_LOGI(TAG, "SD mounted: %s %lluMB", card->cid.name, ((uint64_t)card->csd.capacity * card->csd.sector_size) >> 20);
    return true;
}

void sd_unmount(void)
{
    if (!s_mounted) return;
    bsp_sdcard_unmount();
    s_mounted = false;
}

bool sd_is_mounted(void) { return s_mounted; }

/* TinyUSB MSC takes ownership of the card; FATFS must not touch it meanwhile.
 * The BSP mounts with esp_vfs_fat_sdmmc_mount, so a full unmount would also
 * deinit the card. We keep the card and only drop the VFS/FATFS binding. */
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "ff.h"
void sd_unmount_keep_card(void)
{
    if (!s_mounted) return;
    /* f_mount(NULL) on our drive + unregister VFS; the card stays powered. */
    sdmmc_card_t *card = bsp_sdcard_get_handle();
    BYTE pdrv = ff_diskio_get_pdrv_card(card);
    if (pdrv != 0xFF) {
        char drv[3] = { (char)('0' + pdrv), ':', 0 };
        f_mount(NULL, drv, 0);
        esp_vfs_fat_unregister_path(SD_MOUNT);
        ff_diskio_unregister(pdrv);
    }
    s_mounted = false;
}
void sd_remount_after_msc(void)
{
    /* TinyUSB re-registers the card under our mount path when handed back to
     * the app; if that did not happen, fall back to a full BSP mount. */
    if (fs_is_dir(SD_MOUNT) && sd_space(SD_MOUNT, NULL, NULL)) { s_mounted = true; return; }
    bsp_sdcard_unmount();
    s_mounted = false;
    sd_mount();
}

bool sd_space(const char *path, uint64_t *total, uint64_t *free_bytes)
{
    uint64_t t = 0, f = 0;
    esp_err_t e = esp_vfs_fat_info(path, &t, &f);
    if (e != ESP_OK) return false;
    if (total) *total = t;
    if (free_bytes) *free_bytes = f;
    return true;
}

bool sd_card_info(char *buf, size_t len)
{
    sdmmc_card_t *card = s_mounted ? bsp_sdcard_get_handle() : NULL;
    if (!card) { snprintf(buf, len, "-"); return false; }
    uint64_t cap = (uint64_t)card->csd.capacity * card->csd.sector_size;
    snprintf(buf, len, "%s %s %.1fGB %s", card->cid.name, (card->ocr & (1 << 30)) ? "SDHC/SDXC" : "SDSC",
             cap / 1073741824.0, card->is_ddr ? "DDR" : (card->max_freq_khz > 26000 ? "HS" : "DS"));
    return true;
}

bool fs_exists(const char *path) { struct stat st; return stat(path, &st) == 0; }
bool fs_is_dir(const char *path) { struct stat st; return stat(path, &st) == 0 && S_ISDIR(st.st_mode); }

bool fs_mkdir_p(const char *path)
{
    char tmp[300]; strlcpy(tmp, path, sizeof(tmp));
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = 0; if (!fs_is_dir(tmp)) mkdir(tmp, 0777); *p = '/'; }
    }
    if (!fs_is_dir(tmp)) mkdir(tmp, 0777);
    return fs_is_dir(tmp);
}

int fs_copy_file(const char *src, const char *dst, void (*progress)(uint64_t, uint64_t, void *), void *ud)
{
    FILE *in = fopen(src, "rb");
    if (!in) return -errno;
    struct stat st; fstat(fileno(in), &st);
    FILE *out = fopen(dst, "wb");
    if (!out) { int e = -errno; fclose(in); return e; }
    size_t bufsz = 64 * 1024;
    uint8_t *buf = malloc(bufsz);
    if (!buf) { fclose(in); fclose(out); return -ENOMEM; }
    uint64_t done = 0; int rc = 0;
    size_t n;
    while ((n = fread(buf, 1, bufsz, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { rc = -EIO; break; }
        done += n;
        if (progress) progress(done, st.st_size, ud);
    }
    free(buf);
    fclose(in);
    if (fclose(out) != 0 && rc == 0) rc = -EIO;
    if (rc) unlink(dst);
    return rc;
}

int fs_remove_recursive(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return -errno;
    if (!S_ISDIR(st.st_mode)) return unlink(path) == 0 ? 0 : -errno;
    DIR *d = opendir(path);
    if (!d) return -errno;
    struct dirent *de; int rc = 0;
    char *child = malloc(600);
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        snprintf(child, 600, "%s/%s", path, de->d_name);
        int r = fs_remove_recursive(child);
        if (r && !rc) rc = r;
    }
    free(child);
    closedir(d);
    if (rmdir(path) != 0 && !rc) rc = -errno;
    return rc;
}

int fs_copy_recursive(const char *src, const char *dst, void (*progress)(uint64_t, uint64_t, void *), void *ud)
{
    struct stat st;
    if (stat(src, &st) != 0) return -errno;
    if (!S_ISDIR(st.st_mode)) return fs_copy_file(src, dst, progress, ud);
    if (!fs_mkdir_p(dst)) return -EIO;
    DIR *d = opendir(src);
    if (!d) return -errno;
    struct dirent *de; int rc = 0;
    char *s = malloc(600), *t = malloc(600);
    while ((de = readdir(d)) != NULL && rc == 0) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        snprintf(s, 600, "%s/%s", src, de->d_name);
        snprintf(t, 600, "%s/%s", dst, de->d_name);
        rc = fs_copy_recursive(s, t, progress, ud);
    }
    free(s); free(t);
    closedir(d);
    return rc;
}

uint64_t fs_size_recursive(const char *path, uint32_t *count)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) { if (count) (*count)++; return st.st_size; }
    DIR *d = opendir(path);
    if (!d) return 0;
    uint64_t total = 0; struct dirent *de;
    char *child = malloc(600);
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        snprintf(child, 600, "%s/%s", path, de->d_name);
        total += fs_size_recursive(child, count);
    }
    free(child);
    closedir(d);
    return total;
}

bool fs_read_all(const char *path, char **out, size_t *len, size_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0 || (max && (size_t)sz > max)) { fclose(f); return false; }
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return false; }
    size_t n = fread(buf, 1, sz, f);
    fclose(f);
    buf[n] = 0;
    *out = buf; if (len) *len = n;
    return true;
}

bool fs_write_all(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    if (fclose(f) != 0) ok = false;
    return ok;
}

void fs_unique_name(const char *dir, const char *name, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s/%s", dir, name);
    if (!fs_exists(out)) return;
    char base[200], ext[32] = "";
    strlcpy(base, name, sizeof(base));
    char *dot = strrchr(base, '.');
    if (dot && dot != base) { strlcpy(ext, dot, sizeof(ext)); *dot = 0; }
    for (int i = 1; i < 1000; i++) {
        snprintf(out, out_len, "%s/%s (%d)%s", dir, base, i, ext);
        if (!fs_exists(out)) return;
    }
}

const char *fs_ext(const char *name)
{
    static char ext[16];
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return "";
    size_t i = 0;
    for (const char *p = dot + 1; *p && i < sizeof(ext) - 1; p++) ext[i++] = tolower((unsigned char)*p);
    ext[i] = 0;
    return ext;
}
