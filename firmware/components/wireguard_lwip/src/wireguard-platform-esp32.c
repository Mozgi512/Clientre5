/**
 * @file wireguard-platform-esp32.c
 * @brief ESP32 platform implementation for wireguard-lwip
 */

#include "wireguard-platform.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lwip/sys.h"
#include <string.h>
#include <sys/time.h>

/* ============================================================================
 * Time Functions
 * ========================================================================== */

uint32_t wireguard_sys_now() {
    // Use lwIP's built-in time function
    return sys_now();
}

/* WireGuard handshake init carries a TAI64N timestamp the peer compares
 * against stored greatest_timestamp; anything <= stored is treated as
 * replay and DROPped. ESP boot uptime restarts from 0 each reboot, so
 * unless we add a monotonic offset the peer rejects every handshake we
 * send until its replay-state expires.
 *
 * The base offset is supplied by the higher layer (microlink) via
 * wireguard_set_tai64n_base_seconds() because that layer already owns
 * NVS access and can decide whether to use SNTP wall clock or a
 * persisted per-boot counter. */
static uint64_t s_tai_base_seconds = 0;

void wireguard_set_tai64n_base_seconds(uint64_t base_seconds) {
    s_tai_base_seconds = base_seconds;
}

void wireguard_tai64n_now(uint8_t *output) {
    uint64_t seconds;
    uint32_t nanoseconds;

    /* Prefer the SNTP-synced real wall clock, sampled PER EMIT. It is
     * monotonic across reboots and always far greater than any uptime or
     * per-boot-counter value, so a peer never rejects our handshake initiation
     * as a replay (the WireGuard spec makes the responder DROP any init whose
     * TAI64N timestamp is <= the greatest it has seen from us).
     *
     * Why per-emit and not the cached base: wireguard_set_tai64n_base_seconds()
     * is called ONCE at microlink init — typically BEFORE SNTP has synced — so
     * it falls back to a "+1 day per boot" NVS counter. That counter regresses
     * below the peer's stored timestamp whenever the previous boot ran longer
     * than the increment (e.g. a 6-day uptime emits base+6d, the next boot only
     * sets base+1d), which silently wedges EVERY ESP-initiated handshake until
     * the peer happens to initiate toward us. Sampling the wall clock here, once
     * SNTP has set it, self-heals without any re-init. The cached base remains
     * the pre-sync fallback. */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if ((uint64_t)tv.tv_sec > 1700000000ULL) {
        seconds = (uint64_t)tv.tv_sec;
        nanoseconds = (uint32_t)(tv.tv_usec * 1000);
    } else {
        uint64_t now_us = esp_timer_get_time();
        seconds = s_tai_base_seconds + (now_us / 1000000ULL);
        nanoseconds = (uint32_t)((now_us % 1000000ULL) * 1000);
    }

    /* TAI64 base: 2^62 + Unix time (TAI is +10s from UTC at epoch). */
    seconds += 0x400000000000000AULL;

    output[0] = (seconds >> 56) & 0xFF;
    output[1] = (seconds >> 48) & 0xFF;
    output[2] = (seconds >> 40) & 0xFF;
    output[3] = (seconds >> 32) & 0xFF;
    output[4] = (seconds >> 24) & 0xFF;
    output[5] = (seconds >> 16) & 0xFF;
    output[6] = (seconds >> 8) & 0xFF;
    output[7] = seconds & 0xFF;

    output[8]  = (nanoseconds >> 24) & 0xFF;
    output[9]  = (nanoseconds >> 16) & 0xFF;
    output[10] = (nanoseconds >> 8) & 0xFF;
    output[11] = nanoseconds & 0xFF;
}

/* ============================================================================
 * Random Number Generation
 * ========================================================================== */

void wireguard_random_bytes(void *bytes, size_t size) {
    // Use ESP32 hardware RNG
    esp_fill_random(bytes, size);
}

/* ============================================================================
 * Load Management
 * ========================================================================== */

bool wireguard_is_under_load() {
    // For now, always return false (not under load)
    // Could be enhanced to check:
    // - Free heap memory
    // - CPU usage
    // - Number of active connections
    return false;
}
