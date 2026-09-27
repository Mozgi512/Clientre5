#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    bool valid;
    char make[32], model[48], datetime[24], lens[48], software[32];
    int orientation;          /* 1..8, 0 = unknown */
    uint32_t width, height;   /* from EXIF or SOF */
    double exposure;          /* seconds */
    double fnumber, focal_mm;
    int iso;
    bool has_gps; double lat, lon;
} exif_info_t;
bool exif_parse_file(const char *path, exif_info_t *out);
/* Formats a multi-line description; returns out */
const char *exif_describe(const exif_info_t *e, char *out, int cap);
#ifdef __cplusplus
}
#endif
