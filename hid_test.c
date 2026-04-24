#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "usb.h"
#include "hid.h"
#include "state.h"
#include "mist_cfg.h"

mist_cfg_t mist_cfg;
uint32_t core_type = 0xff;
uint8_t adc_state = 0;

// Usb device descriptor, and report descriptor(s) list
static uint8_t usb_desc_buf[8][USB_MAX_CONFIG_DESC_SIZE];

const usb_device_class_config_t usb_hub_class = {
  USB_HUB, NULL, NULL, NULL
};

const usb_device_class_config_t usb_xbox_class = {
  USB_HID, NULL, NULL, NULL
};

const usb_rtc_class_config_t usb_rtc_tiny_class = {
    .base = { USB_RTC, NULL, NULL, NULL },
};

const usb_rtc_class_config_t usb_rtc_mcp2221_class = {
    .base = { USB_RTC, NULL, NULL, NULL },
};

void timer_delay_msec(uint32_t t) { }

void user_io_digital_joystick_ext(unsigned char joystick, uint32_t map) { }
void user_io_digital_joystick(unsigned char joystick, unsigned char map) { }
void user_io_analog_joystick(unsigned char joystick, int X, int Y, int X2, int Y2) { }
void user_io_kbd(unsigned char m, unsigned char *k, uint8_t priority, unsigned short vid, unsigned short pid) { }
void user_io_mouse(unsigned char idx, unsigned char b, char x, char y, char z) { }

uint8_t joystick_count() { return 0; }
uint8_t joystick_index(uint8_t index) { return index; }
uint8_t joystick_release(uint8_t) { return 0; }
uint8_t joystick_add() { return 1; }

bool virtual_joystick_keyboard( uint16_t vjoy ) { return false; }
uint16_t virtual_joystick_mapping( uint16_t vid, uint16_t pid, uint16_t joy_input ) { return 0; }

void usb_hw_init() { }

uint8_t usb_in_transfer(usb_device_t *, ep_t *, uint16_t *, uint8_t *)
{
    return 0;
}

uint8_t usb_out_transfer(usb_device_t *, ep_t *ep, uint16_t nbytes, const uint8_t* data)
{
    printf("%s: ep%d, report id = 0x%02x\n", __FUNCTION__, ep->epAddr, data[0]);
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
    else if (bRequest == USB_REQUEST_SET_CONFIGURATION)
    {
        if (bmReqType == USB_REQ_SET) {
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

    return 2; /* hrBADREQ */
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
    if (argc < 2)
    {
        printf("Usage: %s usb_dev_desc.dump usb_rpt0_desc.dump [ .. usb_rpt7_desc.dump ]\n", argv[0]);
        return 0;
    }

    // USB HID: loading a list of files with device & report(s) descriptors
    for (int n = 0; n < ARRAY_SIZE(usb_desc_buf) && (n + 1) < argc; n++)
    {
        if (!load_report(usb_desc_buf[n], argv[n + 1]))
        {
            printf("Cannot open %s file: %s\n", argv[n + 1], strerror(errno));
            return errno;
        }
    }

    printf("\n");

    usb_device_t dev;
    usb_device_descriptor_t dev_desc;
    uint8_t rcode;

    memset(&dev, 0, sizeof(usb_device_t));
    dev.ep0.maxPktSize = 8;
    dev.ep0.bmNakPower = USB_NAK_DEFAULT;

    if ((rcode = usb_get_dev_descr(&dev, sizeof(usb_device_descriptor_t), &dev_desc))) {
        return rcode;
    }

    dev.ep0.maxPktSize = dev_desc.bMaxPacketSize0;
    usb_dump_device_descriptor(&dev_desc);

    printf("USB vendor ID: %04X, product ID: %04X\n", dev_desc.idVendor, dev_desc.idProduct);

    // save VID/PID
    dev.vid = dev_desc.idVendor;
    dev.pid = dev_desc.idProduct;

    rcode = usb_hid_class.init(&dev, &dev_desc);

    if (!rcode) {
        return 0;
    }

    printf("USB device NOT accepted, error 0x%X\n", rcode);
    return rcode;
}
