#pragma once
#include "lvgl.h"

/* Requires only a display, not settings/ui_init/fonts_init/services. It uses a
 * fixed Orange EL palette so it can be the first rendered application screen.
 * Call under the display lock. One synchronous draw, no timers or minimum dwell.
 * The normal navigation loader owns deletion when it replaces this screen. */
void ui_boot_show(void);
