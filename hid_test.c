#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include "usb.h"
#include "hid.h"
#include "state.h"
#include "mist_cfg.h"

uint8_t adc_state = 0;
uint32_t core_type = 0xff;
bool osd_is_visible = false;
mist_cfg_t mist_cfg;

// Usb device descriptor, and report descriptor(s) list
static uint8_t usb_desc_buf[8][USB_MAX_CONFIG_DESC_SIZE];

const usb_device_class_config_t usb_hub_class = {
  USB_HUB, NULL, NULL, NULL
};

const usb_rtc_class_config_t usb_rtc_tiny_class = {
    .base = { USB_RTC, NULL, NULL, NULL },
};

const usb_rtc_class_config_t usb_rtc_mcp2221_class = {
    .base = { USB_RTC, NULL, NULL, NULL },
};

void InitRTTC() { }
void usb_hw_init() { }

unsigned long GetRTTC() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000L) + (ts.tv_nsec / 1000000L);
}

void timer_delay_msec(uint32_t delay) {
    usleep(delay * 1000);
}

void user_io_kbd(unsigned char m,
    unsigned char *k, uint8_t priority, unsigned short vid, unsigned short pid)
{
    printf("> KEY = 0x%x\n", *k);
}

void user_io_mouse(unsigned char index, unsigned char btn, char x, char y, char z)
{
    printf("> Mouse%d: X = %d, Y = %d, Z = %d, BTN = %d\n",
        index, x, y, z, btn);
}

void user_io_digital_joystick(unsigned char index, uint32_t map)
{
    printf("> Joy%d: MAP = 0x%x\n", index, map & 0xFF);
    printf("> Joy%d: MAP_EXT = 0x%x\n", index, map);
}

void user_io_analog_joystick(unsigned char index, int X, int Y, int X2, int Y2)
{
    printf("> Joy%d: LX = %d, LY = %d, RX = %d, RY = %d\n",
        index, (int8_t)X, (int8_t)Y, (int8_t)X2, (int8_t)Y2);
}

uint8_t usb_in_transfer(usb_device_t *dev, ep_t *ep, uint16_t *size, uint8_t *buf)
{
    const usb_hid_info_t *info = &(dev->hid_info);

    for (int i = 0; i < info->num_ifaces; i++)
    {
        const usb_hid_iface_info_t *iface = &info->iface[i];

        if (ep == &(iface->ep_in) && *size <= iface->conf.report_size)
        {
            // send junk report
            memset(buf, 0xA5, *size);
            buf[0] = iface->conf.report_id;
            printf("%s: EP%d, report ID = 0x%02x\n", __FUNCTION__, ep->addr, buf[0]);
            // hexdump(buf, *size, 0);
            return 0;
        }
    }

    return 0x02; // hrBADREQ
}

uint8_t usb_out_transfer(usb_device_t *, ep_t *ep, uint16_t nbytes, const uint8_t* data)
{
    printf("%s: EP%d, report ID = 0x%02x\n", __FUNCTION__, ep->addr, data[0]);
    hexdump(data, nbytes, 0);
    return 0;
}

const uint8_t *get_config_desc(uint8_t conf_idx)
{
    const void *p = usb_desc_buf[0] + sizeof(usb_device_descriptor_t);

    do {
        const usb_configuration_descriptor_t *conf_desc = (usb_configuration_descriptor_t *) p;

        if (conf_desc->bDescriptorType != USB_DESCRIPTOR_CONFIGURATION)
            return NULL;

        if (conf_desc->bConfigurationValue == (conf_idx + 1))
            return p;

        // advance to next descriptor
        p += conf_desc->wTotalLength;

    } while (p < (usb_desc_buf[0] + sizeof(usb_desc_buf[0])));

    return NULL;
}

uint8_t usb_ctrl_req(
    usb_device_t *dev, uint8_t bmReqType, uint8_t bRequest,
    uint8_t wValLo, uint8_t wValHi, uint16_t wInd, uint16_t size, uint8_t* buf)
{
    if (bRequest == USB_REQUEST_GET_DESCRIPTOR)
    {
        if (bmReqType == USB_REQ_GET_DESCR
            && wValHi == USB_DESCRIPTOR_DEVICE)
        {
            memcpy(buf, &usb_desc_buf[0], size);
            return 0;
        }
        else if (bmReqType == USB_REQ_GET_DESCR
                && wValHi == USB_DESCRIPTOR_CONFIGURATION)
        {
            const uint8_t *config_desc = get_config_desc(wValLo);

            if (config_desc) {
                memcpy(buf, config_desc, size);
                return 0;
            }

            return 6;
        }
        else if (bmReqType == HID_REQ_HIDREPORT
                && wValHi == HID_DESCRIPTOR_REPORT)
        {
            int rpt_idx = wInd + 1;

            if (rpt_idx < ARRAY_SIZE(usb_desc_buf))
            {
                memcpy(buf, &usb_desc_buf[rpt_idx], size);
                return 0;
            }

            return 5;
        }
    }
    else if (bRequest == USB_REQUEST_SET_CONFIGURATION
            || bRequest == HID_REQUEST_SET_REPORT)
    {
        if (bmReqType == HID_REQ_HIDOUT)
        {
            printf("%s: report ID = 0x%02x, type = 0x%02x\n",
                __FUNCTION__, wValLo, wValHi);
            hexdump(buf, size, 0);
            return 0;
        }
        else if (bmReqType == USB_REQ_SET)
        {
            return 0;
        }
    }
    else if (bRequest == USB_REQUEST_SET_ADDRESS)
    {
        if (bmReqType == USB_REQ_SET) {
            return 0;
        }
    }
    else if (bRequest == HID_REQUEST_SET_IDLE)
    {
        if (bmReqType == HID_REQ_HIDOUT) {
            return 0;
        }
    }

    return 5; /* hrSTALL */
}

uint8_t user_io_swap_joystick(uint8_t joystick)
{
    if (joystick < 2) {
        joystick ^= 1;
    }

    return joystick;
}

static bool load_report(uint8_t *buf, const char* fname)
{
    int fd = open(fname, 0, O_RDONLY);
    if (fd != -1)
    {
        read(fd, buf, USB_MAX_CONFIG_DESC_SIZE);
        close(fd);
        return true;
    }

    return false;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: %s usb_dev_desc.dump usb_rpt0_desc.dump [ .. usb_rpt7_desc.dump ]\n", argv[0]);
        return 0;
    }

    // USB HID: loading a list of files with device & report(s) descriptors
    for (int n = 0; n < ARRAY_SIZE(usb_desc_buf) && (n + 1) < argc; n++)
    {
        if (!load_report(usb_desc_buf[n], argv[n + 1])) {
            printf("Cannot open %s file: %s\n", argv[n + 1], strerror(errno));
            return errno;
        }
    }

    printf("\n");

    usb_device_t dev;
    usb_device_descriptor_t dev_desc;
    uint8_t rcode;

    memset(&dev, 0, sizeof(usb_device_t));
    dev.ep0.nakPower = USB_NAK_DEFAULT;
    dev.ep0.maxPktSize = 8;

    if ((rcode = usb_get_dev_descr(&dev, sizeof(usb_device_descriptor_t), &dev_desc))) {
        printf("Get USB device descriptor, error 0x%x\n", rcode);
        return rcode;
    }

    dev.ep0.maxPktSize = dev_desc.bMaxPacketSize0;
    usb_dump_device_descriptor(&dev_desc);

    printf("USB vendor ID: %04x, product ID: %04x\n", dev_desc.idVendor, dev_desc.idProduct);

    // Save VID/PID
    dev.vid = dev_desc.idVendor;
    dev.pid = dev_desc.idProduct;

    // Driver init
    rcode = usb_hid_class.init(&dev, &dev_desc);
    if (rcode) {
        printf("USB device NOT accepted, error 0x%02x\n", rcode);
        return rcode;
    }

    // Polling loop
    for (int n = 0; n < 3; n++)
    {
        timer_delay_msec(12);
        rcode = usb_hid_class.poll(&dev);

        if (rcode) {
            printf("USB device POLL, error 0x%02x\n", rcode);
        }
    }

    // Driver unload
    rcode = usb_hid_class.release(&dev);
    if (rcode) {
        printf("USB device RELEASE, error 0x%02x\n", rcode);
            return rcode;
    }

    return 0;
}
