#pragma once
static inline int lvgl_port_lock(int x) { return 1; }
static inline void lvgl_port_unlock(void) {}
