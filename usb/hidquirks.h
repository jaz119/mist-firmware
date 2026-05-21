#ifndef HID_QUIRKS_H
#define HID_QUIRKS_H

#include "usb/usb.h"

// Joystick button remap
typedef struct {
    uint8_t idx;    // button index
    uint16_t value; // new value
} joy_btn_remap_t;

// Joystick buttons remap
typedef struct joy_remap_t {
    const joy_btn_remap_t *btn; // remapped buttons list
    uint8_t count;  // count of elements
} joy_remap_t;

// HID device quirks
typedef struct hid_dev_info_t {
    uint16_t vid;
    uint16_t pid;
    const char *name;
    bool (*init_quirk)(usb_device_t *);
    void (*poll_quirk)(usb_device_t *, usb_hid_iface_info_t *, uint8_t *);
    bool (*has)(const usb_interface_descriptor_t *);
    const joy_remap_t remap;
} hid_dev_info_t;

// get HID device info
FAST const hid_dev_info_t* get_hid_dev(uint16_t vid, uint16_t pid);

#endif // HID_QUIRKS_H
