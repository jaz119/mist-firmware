#ifndef USB_RTC_H
#define USB_RTC_H

#include "utils.h"

struct ep_t;
struct usb_device_entry;

// internal time format
typedef uint8_t mtime_t[7];

// i2c bus interface
typedef struct
{
    uint8_t (*bulk_read)(struct usb_device_entry *, uint8_t addr, uint8_t reg, uint8_t *, uint8_t);
    uint8_t (*bulk_write)(struct usb_device_entry *, uint8_t addr, uint8_t reg, uint8_t *, uint8_t);
} i2c_bus_t;

// i2c chip interface
typedef struct
{
    char name[8];
    uint8_t (*probe)(struct usb_device_entry *, const i2c_bus_t *);
    uint8_t (*get_time)(struct usb_device_entry *, const i2c_bus_t *, mtime_t);
    uint8_t (*set_time)(struct usb_device_entry *, const i2c_bus_t *, mtime_t);
} rtc_chip_t;

// usb rtc device driver struct
typedef struct
{
    usb_device_class_config_t entry;
    uint8_t (*get_time)(struct usb_device_entry *, mtime_t);
    uint8_t (*set_time)(struct usb_device_entry *, mtime_t);
} usb_rtc_class_config_t;

// runtime device info
typedef struct
{
    ep_t ep[2];
    uint8_t chip_type;
    uint8_t out_idx;
    uint8_t in_idx;
} usb_rtc_info_t;

uint8_t usb_rtc_get_time(mtime_t);
uint8_t usb_rtc_set_time(mtime_t);

#endif // USB_RTC_H
