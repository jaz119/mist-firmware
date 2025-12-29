#ifndef USB_RTC_H
#define USB_RTC_H

#include "usb.h"

// usb device RTC driver struct
typedef struct
{
    usb_device_class_config_t class;
    uint8_t (*get_time)(struct usb_device_entry *, uint8_t *);
    uint8_t (*set_time)(struct usb_device_entry *, uint8_t *);
} usb_rtc_class_config_t;

uint8_t usb_rtc_get_time(unsigned char *);
uint8_t usb_rtc_set_time(unsigned char *);

#endif // USB_RTC_H
