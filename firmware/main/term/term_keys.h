#pragma once
#include <stddef.h>
#include "input/keyboard.h"
#include "vt.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Translate a key event into the byte sequence a terminal sends. Returns length. */
size_t term_keys_encode(const key_event_t *ev, const vt_t *vt, char *out, size_t cap);
#ifdef __cplusplus
}
#endif
