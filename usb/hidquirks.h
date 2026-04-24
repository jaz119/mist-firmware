#ifndef HID_QUIRKS_H
#define HID_QUIRKS_H

#include "usb/usb.h"

// HID device quirks
typedef struct {
    uint16_t vid;
    uint16_t pid;
    char name[21];
    void (*init_quirk)(usb_device_t *);
    void (*poll_quirk)(usb_device_t *, usb_hid_iface_info_t *, uint8_t *);
} hid_dev_info_t;

// get HID device info
const hid_dev_info_t* get_hid_dev(uint16_t vid, uint16_t pid);

#endif // HID_QUIRKS_H
