#ifndef USB_RTC_H
#define USB_RTC_H

#include "usb.h"
#include "utils.h"

typedef uint8_t timestamp_t[7];

// I2C bus interface
typedef struct
{
    uint8_t (*bulk_read)(usb_device_t *dev, uint8_t addr, uint8_t reg, uint8_t *, uint8_t length);
    uint8_t (*bulk_write)(usb_device_t *dev, uint8_t addr, uint8_t reg, uint8_t *, uint8_t length);
} i2c_bus_t;

// I2C RTC chip interface
typedef struct
{
    uint8_t name[8];
    uint8_t (*probe)(usb_device_t *, const i2c_bus_t *);
    uint8_t (*get_time)(usb_device_t *, const i2c_bus_t *, timestamp_t);
    uint8_t (*set_time)(usb_device_t *, const i2c_bus_t *, timestamp_t);
} rtc_chip_t;

// usb device RTC driver struct
typedef struct
{
    usb_device_class_config_t class;
    uint8_t (*get_time)(struct usb_device_entry *, timestamp_t);
    uint8_t (*set_time)(struct usb_device_entry *, timestamp_t);
} usb_rtc_class_config_t;

uint8_t usb_rtc_get_time(timestamp_t);
uint8_t usb_rtc_set_time(timestamp_t);

#endif // USB_RTC_H
