#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
void webfm_init(void);
bool webfm_start(void);
void webfm_stop(void);
bool webfm_running(void);
#ifdef __cplusplus
}
#endif
