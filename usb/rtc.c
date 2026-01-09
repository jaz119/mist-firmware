//
// usb/rtc.c
//
// interface for RTC USB device drivers
//

#include "usb.h"
#include "usb/rtc.h"

bool usb_rtc_get_time(ctime_t date)
{
    // find last connected RTC device
    usb_device_t *dev = usb_get_last_device(USB_RTC);

    if (dev && date)
    {
        const usb_rtc_class_config_t *rtc = (usb_rtc_class_config_t *) dev->class;
        return rtc->get_time(dev, date);
    }

    return false;
}

bool usb_rtc_set_time(const ctime_t date)
{
    // find last connected RTC device
    usb_device_t *dev = usb_get_last_device(USB_RTC);

    if (dev && date)
    {
        const usb_rtc_class_config_t *rtc = (usb_rtc_class_config_t *) dev->class;
        return rtc->set_time(dev, date);
    }

    return false;
}
