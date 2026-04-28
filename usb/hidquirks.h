#ifndef HID_QUIRKS_H
#define HID_QUIRKS_H

#include "usb/usb.h"

// Vendor's Interface identification
typedef struct {
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
} usb_target_iface_t;

// HID device quirks
typedef struct {
    uint16_t vid;
    uint16_t pid;
    char name[24];
    void (*init_quirk)(usb_device_t *);
    void (*poll_quirk)(usb_device_t *, usb_hid_iface_info_t *, uint8_t *);
    bool (*have)(const usb_interface_descriptor_t *);
} hid_dev_info_t;

// get HID device info
const hid_dev_info_t* get_hid_dev(uint16_t vid, uint16_t pid);

#endif // HID_QUIRKS_H
