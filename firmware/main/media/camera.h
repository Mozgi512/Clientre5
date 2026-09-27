#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CAMERA_STATE_OFF = 0,
    CAMERA_STATE_STARTING,
    CAMERA_STATE_RUNNING,
    CAMERA_STATE_ERROR,
} camera_state_t;

typedef enum {
    CAMERA_PHOTO_IDLE = 0,
    CAMERA_PHOTO_PENDING,
    CAMERA_PHOTO_SAVED,
    CAMERA_PHOTO_ERROR,
} camera_photo_state_t;

bool camera_start(void);
void camera_stop(void);
camera_state_t camera_state(void);
const char *camera_error(void);

/* RGB565 preview. Returns false when no newer complete frame is available. */
bool camera_copy_frame(void *dst, size_t capacity, uint32_t *width,
                       uint32_t *height, uint32_t *sequence);

bool camera_take_photo(void);
camera_photo_state_t camera_photo_state(char *path, size_t path_len);

#ifdef __cplusplus
}
#endif
