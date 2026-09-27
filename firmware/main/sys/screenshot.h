#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
void screenshot_init(void);                /* three-finger tap detector (LVGL task) */
bool screenshot_take(char *out_path, int cap);
#ifdef __cplusplus
}
#endif
