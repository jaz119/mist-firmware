//
// usb/rtc.c
//
// interface for RTC device drivers
//

#include "usb/rtc.h"

uint8_t usb_rtc_get_time(timestamp_t date)
{
    // find last connected RTC device
    usb_device_t *dev = usb_get_last_device(USB_RTC);

    if (dev)
    {
        usb_rtc_class_config_t *rtc = (usb_rtc_class_config_t *) dev->class;
        return rtc->get_time(dev, date);
    }

    return 0;
}

uint8_t usb_rtc_set_time(timestamp_t date)
{
    // find last connected RTC device
    usb_device_t *dev = usb_get_last_device(USB_RTC);

    if (dev)
    {
        usb_rtc_class_config_t *rtc = (usb_rtc_class_config_t *) dev->class;
        return rtc->set_time(dev, date);
    }

    return 0;
}
