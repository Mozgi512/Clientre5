# Firmware UI preview

A headless macOS renderer that compiles the actual LVGL, fonts, theme, home,
settings and appearance sources. It uses mock hardware responses and stub screens
for unrelated features. It does not connect to WiFi, Tailscale or a device, and
does not verify touch hardware, networking, audio or GPU/display performance.

Requires CMake, Ninja, a C/C++ compiler and the managed LVGL component downloaded
by the firmware build. Run from the repository root:

```sh
cmake -S firmware/tools/ui_preview -B /tmp/tab5-ui-preview -G Ninja
cmake --build /tmp/tab5-ui-preview -j 8
cd /tmp/tab5-ui-preview
./preview home 0 1280 720
sips -s format png screen.ppm --out home.png
./preview settings 0 1280 720 1
sips -s format png screen.ppm --out settings.png
./preview settings 8 1280 720 1
sips -s format png screen.ppm --out classic.png
./preview home 0 720 1280
./preview settings 0 720 1280 3
./preview appearance 0 1280 720
```

Arguments: screen (`home`, `settings`, `appearance`), preset index (0 = Orange EL,
8 = Classic), width, height, settings category (0–6). Output: `screen.ppm` in the
working directory; Japanese labels and simulated status values are used.

Before each capture, the renderer cycles all presets through live theme rebuilds.
Settings sends real LVGL click events through all seven categories and asserts
that exactly the selected panel remains visible. Home checks all ten tiles fit
within the display. The normal firmware build remains the compile/link check for
all other screens and the ESP32-P4 target.

## Motion / redraw regression checks

```sh
./preview motion 0 1280 720
./preview motion 0 720 1280
UI_CAPTURE_MOTION=1 ./preview motion 0 1280 720  # optional motion-00..12.ppm frames
```

The preview now uses partial 50-line buffers, as on the board. The motion check
counts pixels submitted to the display flush callback, compares an EL sweep
against the previous 150 ms slide using the **same current widgets**, asserts
that idle animation invalidations stop, and interrupts sweeps by navigation,
theme switching and screen deletion. Settings checks also assert that clicking
a category keeps the same screen object.

Measured host redraw workload (includes the initial new-screen draw):

| Resolution | EL sweep pixels | Legacy slide pixels | Largest sweep frame after initial draw |
|---|---:|---:|---:|
| 1280 × 720 | 3,522,560 | 4,608,000 | 288,000 (31.25%) |
| 720 × 1280 | 3,517,200 | 4,608,000 | 286,560 (31.09%) |

These are drawing-area measurements, **not ESP32-P4 FPS or latency measurements**.
The host uses 32-bit pixels and synchronous mock flush completion. Real panel,
PSRAM, RGB565 blending, PPA/DMA and network load must be profiled on the device.
The comparison isolates the transition; it does not quantify the additional
benefit of removing blurred shadows or avoiding category screen rebuilds.

## Early boot splash

```sh
./preview boot 0 1280 720
./preview boot 0 720 1280
./preview boot 8 1280 720
```

This path draws before `fonts_init()` and `ui_init()`, checks that the static
splash triggers no periodic redraw, captures it to `screen.ppm`, then initializes
the UI and checks the navigation handoff. It uses no hardware/service mocks to
construct the logo. Startup duration and the real LCD/backlight handoff still
require a physical Tab5 check.
