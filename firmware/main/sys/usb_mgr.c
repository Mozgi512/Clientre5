/* USB: host mode (HID keyboard + MSC flash drive) and device mode (TF card as USB disk). */
#include "usb_mgr.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"
#include "soc/lp_system_struct.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/m5stack_tab5.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "storage/sd.h"
#include "media/audio_player.h"
#include "web/webfm.h"
#include "input/keyboard.h"
#include "i18n/i18n.h"

static const char *TAG = "usb";
static usb_mode_t s_mode = USB_MODE_HOST;
static bool s_host_started = false;
static msc_host_device_handle_t s_msc_dev = NULL;
static msc_host_vfs_handle_t s_msc_vfs = NULL;
static volatile uint8_t s_msc_addr = 0;
static bool s_msc_present = false, s_msc_mounted = false;
static tinyusb_msc_storage_handle_t s_disk = NULL;
static bool s_disk_mode = false;
static char s_status[96] = "USB host";
static TaskHandle_t s_lib_task = NULL;
static volatile bool s_dev_attached = false;
static const char *s_disk_note = "";

/* ---------------- HID ---------------- */
static void hid_iface_cb(hid_host_device_handle_t dev, const hid_host_interface_event_t ev, void *arg)
{
    (void)arg;
    if (ev == HID_HOST_INTERFACE_EVENT_INPUT_REPORT) {
        uint8_t rep[64]; size_t len = 0;
        if (hid_host_device_get_raw_input_report_data(dev, rep, sizeof(rep), &len) == ESP_OK) {
            hid_host_dev_params_t p;
            if (hid_host_device_get_params(dev, &p) == ESP_OK && p.proto == HID_PROTOCOL_KEYBOARD) keyboard_usb_hid_report(rep, len);
        }
    } else if (ev == HID_HOST_INTERFACE_EVENT_DISCONNECTED) {
        hid_host_device_close(dev);
        ESP_LOGI(TAG, "HID device disconnected");
    }
}
static void hid_driver_cb(hid_host_device_handle_t dev, const hid_host_driver_event_t ev, void *arg)
{
    (void)arg;
    if (ev != HID_HOST_DRIVER_EVENT_CONNECTED) return;
    hid_host_dev_params_t p;
    if (hid_host_device_get_params(dev, &p) != ESP_OK) return;
    const hid_host_device_config_t cfg = { .callback = hid_iface_cb, .callback_arg = NULL };
    if (hid_host_device_open(dev, &cfg) != ESP_OK) return;
    if (p.sub_class == HID_SUBCLASS_BOOT_INTERFACE && p.proto == HID_PROTOCOL_KEYBOARD) {
        hid_class_request_set_protocol(dev, HID_REPORT_PROTOCOL_BOOT);
        hid_class_request_set_idle(dev, 0, 0);
        ESP_LOGI(TAG, "USB keyboard connected");
    }
    hid_host_device_start(dev);
}

/* ---------------- MSC (flash drive) ---------------- */
static void msc_cb(const msc_host_event_t *ev, void *arg)
{
    (void)arg;
    if (ev->event == MSC_DEVICE_CONNECTED) { s_msc_addr = ev->device.address; s_msc_present = true; }
    else if (ev->event == MSC_DEVICE_DISCONNECTED) { s_msc_present = false; s_msc_addr = 0; }
}

static void mount_msc(void)
{
    if (s_msc_mounted || !s_msc_addr) return;
    if (msc_host_install_device(s_msc_addr, &s_msc_dev) != ESP_OK) { ESP_LOGW(TAG, "msc install device failed"); return; }
    esp_vfs_fat_mount_config_t mc = { .format_if_mount_failed = false, .max_files = 6, .allocation_unit_size = 16 * 1024 };
    esp_err_t e = msc_host_vfs_register(s_msc_dev, USB_MOUNT, &mc, &s_msc_vfs);
    if (e != ESP_OK) { ESP_LOGW(TAG, "USB drive mount failed: %s", esp_err_to_name(e)); snprintf(s_status, sizeof(s_status), "USB drive: unformatted or unsupported"); return; }
    s_msc_mounted = true;
    snprintf(s_status, sizeof(s_status), "USB drive mounted at %s", USB_MOUNT);
    ESP_LOGI(TAG, "%s", s_status);
}
static void unmount_msc(void)
{
    if (s_msc_vfs) { msc_host_vfs_unregister(s_msc_vfs); s_msc_vfs = NULL; }
    if (s_msc_dev) { msc_host_uninstall_device(s_msc_dev); s_msc_dev = NULL; }
    if (s_msc_mounted) ESP_LOGI(TAG, "USB drive removed");
    s_msc_mounted = false;
    snprintf(s_status, sizeof(s_status), "USB host");
}

static void usb_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_mode == USB_MODE_HOST) {
            if (s_msc_present && !s_msc_mounted && s_msc_addr) mount_msc();
            if (!s_msc_present && (s_msc_mounted || s_msc_dev)) unmount_msc();
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

/* The host library's event loop is ours rather than the BSP's: bsp_usb_host_stop()
 * uninstalls the library from under its own task, and bsp_usb.c's task wraps
 * usb_host_device_free_all() in ESP_ERROR_CHECK() -- that call returns
 * ESP_ERR_NOT_FINISHED whenever a device still has to be freed, which is the
 * normal answer rather than a failure, so the BSP aborts and the board reboots. */
static void usb_lib_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t flags = 0;
        esp_err_t e = usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (e == ESP_ERR_INVALID_STATE) break;
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            e = usb_host_device_free_all();
            if (e != ESP_OK && e != ESP_ERR_NOT_FINISHED) ESP_LOGW(TAG, "free all: %s", esp_err_to_name(e));
        }
    }
    s_lib_task = NULL;
    vTaskDelete(NULL);
}

static bool host_start(void)
{
    if (s_host_started) return true;
    if (bsp_feature_enable(BSP_FEATURE_USB, true) != ESP_OK) ESP_LOGW(TAG, "usb power enable failed");
    const usb_host_config_t hostcfg = { .skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LOWMED };
    esp_err_t e = usb_host_install(&hostcfg);
    if (e != ESP_OK) { ESP_LOGW(TAG, "usb_host_install: %s", esp_err_to_name(e)); return false; }
    if (xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 10, &s_lib_task) != pdPASS) {
        ESP_LOGE(TAG, "usb lib task failed");
        usb_host_uninstall();
        return false;
    }
    const hid_host_driver_config_t hc = { .create_background_task = true, .task_priority = 5, .stack_size = 4096, .core_id = 1, .callback = hid_driver_cb };
    if (hid_host_install(&hc) != ESP_OK) ESP_LOGW(TAG, "hid host install failed");
    const msc_host_driver_config_t mcfg = { .create_backround_task = true, .task_priority = 5, .stack_size = 4096, .core_id = 1, .callback = msc_cb };
    if (msc_host_install(&mcfg) != ESP_OK) ESP_LOGW(TAG, "msc host install failed");
    s_host_started = true;
    s_mode = USB_MODE_HOST;
    return true;
}

void usb_mgr_init(void)
{
    host_start();
    xTaskCreate(usb_task, "usb_mgr", 4096, NULL, 3, NULL);
}

usb_mode_t usb_mgr_mode(void) { return s_mode; }
bool usb_mgr_msc_mounted(void) { return s_msc_mounted; }
bool usb_mgr_msc_present(void) { return s_msc_present; }
const char *usb_mgr_status_text(void)
{
    if (!s_disk_mode) return s_status;
    static char b[128];
    snprintf(b, sizeof(b), "USB disk mode: %s%s%s",
             s_dev_attached ? "PC connected" : "connect USB-C to the PC",
             s_disk_note[0] ? " - " : "", s_disk_note);
    return b;
}

bool usb_mgr_eject_msc(void)
{
    if (!s_msc_mounted) return false;
    unmount_msc();
    return true;
}

bool usb_mgr_format_msc(void)
{
    if (!s_msc_present) return false;
    if (!s_msc_dev) { if (msc_host_install_device(s_msc_addr, &s_msc_dev) != ESP_OK) return false; }
    if (s_msc_vfs) { msc_host_vfs_unregister(s_msc_vfs); s_msc_vfs = NULL; s_msc_mounted = false; }
    esp_vfs_fat_mount_config_t mc = { .format_if_mount_failed = true, .max_files = 6, .allocation_unit_size = 16 * 1024 };
    esp_err_t e = msc_host_vfs_format(s_msc_dev, &mc, NULL);
    if (e != ESP_OK) { ESP_LOGW(TAG, "format failed: %s", esp_err_to_name(e)); return false; }
    e = msc_host_vfs_register(s_msc_dev, USB_MOUNT, &mc, &s_msc_vfs);
    s_msc_mounted = (e == ESP_OK);
    return s_msc_mounted;
}

/* ---------------- device mode: TF card as USB disk ---------------- */

/* The ESP32-P4 has two internal full-speed PHYs and LP_SYS.usb_ctrl decides who
 * gets which: by default the USB-Serial-JTAG owns PHY 0 and the OTG 1.1
 * controller owns PHY 1. On the Tab5 the USB-C data lines are on PHY 0
 * (GPIO24/25) and PHY 1 (GPIO26/27) is not wired to anything, so a device stack
 * on OTG 1.1 enumerates into thin air until the two are swapped. Swapping moves
 * the serial console onto the unconnected PHY, which is why USB serial drops for
 * as long as disk mode runs. */
/* Reported on the USB screen: with the serial console off the air during disk
 * mode, this is the only way to tell "the PC never enumerated us" apart from
 * "the PC enumerated us but refused to mount the volume". */
static void disk_event_cb(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    if (event->id == TINYUSB_EVENT_ATTACHED) { s_dev_attached = true; ESP_LOGI(TAG, "disk mode: host attached (rhport %u)", event->rhport); }
    else if (event->id == TINYUSB_EVENT_DETACHED) { s_dev_attached = false; ESP_LOGI(TAG, "disk mode: host detached"); }
}

static void disk_storage_cb(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t *event, void *arg)
{
    (void)handle; (void)arg;
    switch (event->id) {
    case TINYUSB_MSC_EVENT_MOUNT_FAILED:     s_disk_note = "TF card could not be handed over"; break;
    case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:  s_disk_note = "TF card is not formatted"; break;
    case TINYUSB_MSC_EVENT_FORMAT_FAILED:    s_disk_note = "format failed"; break;
    default:
        ESP_LOGI(TAG, "disk mode storage event %d (mount point %d)", event->id, event->mount_point);
        return;
    }
    ESP_LOGW(TAG, "disk mode storage: %s", s_disk_note);
}

static void usb_c_phy_to_otg(bool to_otg)
{
    uint32_t prev = LP_SYS.usb_ctrl.val;
    LP_SYS.usb_ctrl.sw_hw_usb_phy_sel = to_otg;   /* 1: software picks the mapping */
    LP_SYS.usb_ctrl.sw_usb_phy_sel = to_otg;      /* 1: OTG 1.1 -> PHY 0 (USB-C) */
    ESP_LOGI(TAG, "USB-C full-speed PHY -> %s (usb_ctrl 0x%08" PRIx32 " -> 0x%08" PRIx32 ")",
             to_otg ? "OTG1.1/TinyUSB, serial console drops" : "USB-Serial-JTAG",
             prev, LP_SYS.usb_ctrl.val);
}

bool usb_mgr_start_disk_mode(void)
{
    if (s_disk_mode) return true;
    if (!sd_is_mounted()) return false;
    sdmmc_card_t *card = bsp_sdcard_get_handle();
    if (!card) return false;
    /* CONFIG_TINYUSB_MSC_BUFSIZE must stay at or below 8128 bytes here. The MSC
     * class hands its whole buffer to one dwc2 transfer and dcd_dwc2.c writes
     * the packet count straight into DIEPTSIZ/DOEPTSIZ without clamping it to
     * what the core implements. OTG 1.1 on the ESP32-P4 has a 7-bit packet
     * count (soc/usb_dwc_cfg.h: OTG11_PACKET_COUNT_WIDTH 7), so with 64-byte
     * full-speed bulk packets the ceiling is 127 packets = 8128 bytes. A 32 KB
     * buffer asks for 512 packets, which truncates to exactly 0 and the
     * transfer never completes: the host then times out after 30 s and resets
     * the port, over and over, while control transfers keep working.
     *
     * The ESP32-P4 has two independent OTG controllers. The host library always
     * takes the high-speed one (usb_host.c forces the UTMI PHY), which is the
     * USB-A receptacle, so the device stack goes on the full-speed controller
     * behind USB-C and the USB keyboard keeps working. TINYUSB_DEFAULT_CONFIG()
     * would pick the high-speed port here, i.e. the wrong connector. Note that
     * this takes the USB-Serial-JTAG off the bus until disk mode is stopped. */
    tinyusb_msc_driver_config_t dc = { .callback = disk_storage_cb };
    if (tinyusb_msc_install_driver(&dc) != ESP_OK) { ESP_LOGE(TAG, "msc driver install failed"); goto fail; }
    /* Hand the card to TinyUSB: unmount our FATFS first so the PC gets exclusive access. */
    sd_unmount_keep_card();
    tinyusb_msc_storage_config_t sc = { .medium.card = card, .fat_fs = { .base_path = SD_MOUNT, .config = { .max_files = 4, .format_if_mount_failed = false }, .do_not_format = true }, .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB };
    if (tinyusb_msc_new_storage_sdmmc(&sc, &s_disk) != ESP_OK) { ESP_LOGE(TAG, "msc storage failed"); tinyusb_msc_uninstall_driver(); goto fail; }
    /* Both of these read the TF card, so stop them while the PC owns it. */
    if (audio_player_state() != AP_STOPPED) { ESP_LOGI(TAG, "stopping audio before disk mode"); audio_player_stop(); }
    if (webfm_running()) { ESP_LOGI(TAG, "stopping web file manager before disk mode"); webfm_stop(); }
    s_dev_attached = false; s_disk_note = "";
    usb_c_phy_to_otg(true);
    tinyusb_config_t tc = TINYUSB_CONFIG_FULL_SPEED(disk_event_cb, NULL);
    /* The default is priority 5 on core 1, where MicroLink's ml_wg_mgr (prio 7)
     * and ml_coord (prio 5) also live. Starving the device task makes control
     * and bulk transfers late, and the host answers by resetting the port --
     * which showed up as the device re-attaching every second. Run above
     * MicroLink but below lwIP (18). The stack is bigger than the default 4 KB
     * because tud_msc_read10_cb enters the SDMMC driver from this task. */
    tc.task = TINYUSB_TASK_CUSTOM(8192, 10, TINYUSB_DEFAULT_TASK_AFFINITY);
    if (tinyusb_driver_install(&tc) != ESP_OK) { ESP_LOGE(TAG, "tinyusb install failed"); usb_c_phy_to_otg(false); tinyusb_msc_delete_storage(s_disk); s_disk = NULL; tinyusb_msc_uninstall_driver(); goto fail; }
    s_disk_mode = true;
    uint32_t sectors = 0, sec_size = 0;
    tinyusb_msc_get_storage_capacity(s_disk, &sectors);
    tinyusb_msc_get_storage_sector_size(s_disk, &sec_size);
    ESP_LOGI(TAG, "USB disk mode started, %" PRIu32 " MB exposed to the host",
             (uint32_t)(((uint64_t)sectors * sec_size) >> 20));
    return true;
fail:
    sd_mount();
    return false;
}

bool usb_mgr_stop_disk_mode(void)
{
    if (!s_disk_mode) return true;
    tinyusb_msc_set_storage_mount_point(s_disk, TINYUSB_MSC_STORAGE_MOUNT_APP);
    tinyusb_driver_uninstall();
    usb_c_phy_to_otg(false);
    s_dev_attached = false; s_disk_note = "";
    tinyusb_msc_delete_storage(s_disk); s_disk = NULL;
    tinyusb_msc_uninstall_driver();
    s_disk_mode = false;
    sd_remount_after_msc();
    ESP_LOGI(TAG, "USB disk mode stopped");
    return sd_is_mounted();
}
bool usb_mgr_disk_mode_active(void) { return s_disk_mode; }
