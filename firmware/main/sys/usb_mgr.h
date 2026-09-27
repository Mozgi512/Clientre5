#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { USB_MODE_HOST = 0, USB_MODE_DEVICE_MSC } usb_mode_t;
void usb_mgr_init(void);                 /* starts USB host (HID keyboard + MSC flash drive) */
usb_mode_t usb_mgr_mode(void);
bool usb_mgr_msc_mounted(void);          /* flash drive at /usb */
bool usb_mgr_msc_present(void);
bool usb_mgr_start_disk_mode(void);      /* expose TF card over USB-C (TinyUSB MSC) */
bool usb_mgr_stop_disk_mode(void);
bool usb_mgr_disk_mode_active(void);
bool usb_mgr_format_msc(void);           /* FAT32 */
bool usb_mgr_eject_msc(void);
const char *usb_mgr_status_text(void);
#ifdef __cplusplus
}
#endif
