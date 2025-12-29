//
// usb/rtc.c
//
// interface for RTC device drivers
//

#include "rtc.h"

uint8_t usb_rtc_get_time(uint8_t *date)
{
    // find first RTC device
    usb_device_t *dev = usb_get_device_type(USB_RTC);

    if (dev)
    {
        usb_rtc_class_config_t *rtc = (usb_rtc_class_config_t *) dev;
        return rtc->get_time(dev, date);
    }

    return 0;
}

uint8_t usb_rtc_set_time(uint8_t *date)
{
    // find first RTC device
    usb_device_t *dev = usb_get_device_type(USB_RTC);

    if (dev)
    {
        usb_rtc_class_config_t *rtc = (usb_rtc_class_config_t *) dev;
        return rtc->set_time(dev, date);
    }

    return 0;
}
