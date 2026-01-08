//
// i2c-mcp2221.c
//
// RTC driver MCP2221(A) USB/I2C bridge chip
//

#include <string.h>

#include "usb.h"
#include "max3421e.h"
#include "rtc/i2c-mcp2221.h"
#include "rtc/pcf85x63.h"
#include "rtc/ds3231.h"
#include "timer.h"
#include "debug.h"

#define REPORT_SIZE     64

#define MCP2221_VID     0x04d8
#define MCP2221_PID     0x00dd

// commands codes
enum {
    CMD_I2C_WR_DATA = 0x90,
    CMD_I2C_WR_RPT_START = 0x92,
    CMD_I2C_WR_NO_STOP = 0x94,
    CMD_I2C_RD_DATA = 0x91,
    CMD_I2C_RD_RPT_START = 0x93,
    CMD_I2C_GET_DATA = 0x40,
    CMD_I2C_PARAM_OR_STATUS = 0x10,
};

// status/set parameters command
typedef struct {
    uint8_t  cmd_code;              // 0x10 = CMD_I2C_PARAM_OR_STATUS
    uint8_t  unused1;               // Any value
    uint8_t  cancel_i2c;            // 0x10 = Cancel the current I2C transfer
    uint8_t  set_i2c_speed;         // 0x20 = Set the I2C communication speed
    uint8_t  i2c_clock_divider;     // Value of the I2C system clock divider
    uint8_t  unused2[59];           // Any values
} __attribute__ ((packed)) mcp_cmd_t;

// status/set parameters response
typedef struct {
    uint8_t  cmd_echo;              // 0x10 = CMD_I2C_PARAM_OR_STATUS
    uint8_t  is_failed;             // 0x00 = Command completed successfully
    uint8_t  cancel_i2c;            // 0x00 = No special operation
                                    // 0x10 = Transfer was marked for cancellation
                                    // 0x11 = Already in Idle mode
    uint8_t  set_i2c_speed;         // 0x00 = No special operation
                                    // 0x20 = New communication speed is being set
                                    // 0x21 = Speed change rejected
    uint8_t  i2c_req_divider;       // Value of the I2C system clock divider
    uint8_t  unused1[3];            // Don’t care
    uint8_t  i2c_machine_state;     // Internal I2C state machine state value
    uint16_t i2c_transfer_length;   // Requested I2C transfer length
    uint16_t i2c_transfered;        // Number of already transferred bytes
    uint8_t  i2c_buf_count;         // Internal I2C data buffer counter
    uint8_t  i2c_div;               // Current I2C speed divider
    uint8_t  i2c_timeout;           // Current I2C timeout value
    uint16_t i2c_address;           // I2C address being used
    uint8_t  unused2[4];            // Don’t care
    uint8_t  scl_input;             // SCL line value, as read from the pin
    uint8_t  sda_input;             // SDA line value, as read from the pin
    uint8_t  intr_edge;             // Interrupt edge detector state, 0 or 1
    uint8_t  i2c_read_pending;      // 0, 1 or 2
    uint8_t  unused3[20];           // Don’t care
    uint8_t  hw_rev_major;          // ‘A’
    uint8_t  hw_rev_minor;          // ‘6’
    uint8_t  fw_rev_major;          // ‘1’
    uint8_t  fw_rev_minor;          // ‘1’
    uint16_t adc_ch0;               // ADC channel 0 input value
    uint16_t adc_ch1;               // ADC channel 1 input value
    uint16_t adc_ch2;               // ADC channel 2 input value
    uint8_t  unused4[8];            // Don’t care
} __attribute__ ((packed)) mcp_cmd_resp_t;

// slave i2c command
typedef struct {
    uint8_t  cmd_code;              // I2C command code
    uint8_t  size_low;              // I2C transfer length, low byte
    uint8_t  size_high;             // I2C transfer length, high byte
    uint8_t  slave_addr;            // I2C slave address to communicate with
    uint8_t  data[60];              // Data buffer for write
} __attribute__ ((packed)) mcp_i2c_cmd_t;

// slave i2c response
typedef struct {
    uint8_t  cmd_echo;              // I2C command code echo
    uint8_t  is_failed;             // 0x00 = Completed successfully, 0x01 = Not completed
    uint8_t  internal_state;        // Internal I2C Engine state or Reserved
    uint8_t  data_size;             // Data size or Don’t care
    uint8_t  data[60];              // Data buffer for read or Don’t care
} __attribute__ ((packed)) mcp_i2c_resp_t;

static bool mcp_i2c_bulk_read(
    usb_device_t *, uint8_t, uint8_t, uint8_t *, uint8_t);

static bool mcp_i2c_bulk_write(
    usb_device_t *, uint8_t, uint8_t, uint8_t *, uint8_t);

static const i2c_bus_t mcp_i2c_bus = {
  .bulk_read = mcp_i2c_bulk_read,
  .bulk_write = mcp_i2c_bulk_write
};

// all of supported RTCs list
static const rtc_chip_t *rtc_chips[] = {
    &rtc_ds3231_chip,
    &rtc_pcf85x63_chip,
    NULL
};

static bool mcp_exec(usb_device_t *dev, uint8_t *rpt, uint16_t *size)
{
    uint8_t rcode, cmd = rpt[0];
    mcp_rtc_info_t *info = &(dev->mcp_rtc_info);

    rcode = usb_out_transfer(dev, &info->ep_out, REPORT_SIZE, rpt);

    if (rcode) {
        usbrtc_debugf("%s: OUT failed for #%X, error #%X", __FUNCTION__, rpt[0], rcode);
        // return false;
    }

    *size = REPORT_SIZE;
    rpt[0] = rpt[1] = -1;

    rcode = usb_in_transfer(dev, &info->ep_in, size, rpt);

    // check for command echo and status code
    if (rcode || rpt[0] != cmd || rpt[1] != 0) {
        usbrtc_debugf("%s: IN failed for #%X, error #%X", __FUNCTION__, cmd, rcode);
        return false;
    }

    return true;
}

static bool mcp_set_i2c_clock(usb_device_t *dev, uint16_t clock)
{
    uint16_t size;
    mcp_rtc_info_t *info = &(dev->mcp_rtc_info);

    union {
        mcp_cmd_t cmd;
        mcp_cmd_resp_t resp;
        uint8_t raw[REPORT_SIZE];
    } pkt;

    if (info->i2c_clock == clock)
        return true;

    if (clock > 400) clock = 400;
    if (clock < 47)  clock = 47;

    pkt.cmd.cmd_code = CMD_I2C_PARAM_OR_STATUS;
    pkt.cmd.i2c_clock_divider = (12000000U - clock*1000) - 3;
    pkt.cmd.set_i2c_speed = 0x20;
    pkt.cmd.cancel_i2c = 0x10;
    pkt.cmd.unused1 = 0;

    if (mcp_exec(dev, pkt.raw, &size))
    {
        if (pkt.resp.set_i2c_speed == 0x20)
        {
            info->i2c_clock = clock;
            return true;
        } else {
            usbrtc_debugf("%s: mcp2221 error #%X",
                __FUNCTION__, pkt.resp.set_i2c_speed);
        }
    }

    return false;
}

static bool mcp_check_status(usb_device_t *dev)
{
    uint16_t size;

    union {
        mcp_cmd_t cmd;
        mcp_cmd_resp_t resp;
        uint8_t raw[REPORT_SIZE];
    } pkt;

    pkt.cmd.cmd_code = CMD_I2C_PARAM_OR_STATUS;
    pkt.cmd.set_i2c_speed = 0;
    pkt.cmd.cancel_i2c = 0;
    pkt.cmd.unused1 = 0;

    if (!mcp_exec(dev, pkt.raw, &size) || size != REPORT_SIZE)
        return false;

    iprintf("mcp2221 chip detected, rev.%c%c, fw: %c.%c, i2c clock div: %u\n",
        pkt.resp.hw_rev_major, pkt.resp.hw_rev_minor,
        pkt.resp.fw_rev_major, pkt.resp.fw_rev_minor,
        pkt.resp.i2c_div);

    return true;
}

static uint8_t usb_hid_parse_conf(
    usb_device_t *dev, uint8_t conf, uint16_t len)
{
    uint8_t rcode;
    mcp_rtc_info_t *info = &(dev->mcp_rtc_info);
    bool isHID = false;

    union buf_u {
        usb_configuration_descriptor_t conf_desc;
        usb_interface_descriptor_t iface_desc;
        usb_endpoint_descriptor_t ep_desc;
        uint8_t raw[len];
    } buf, *p;

    // usb_interface_descriptor
    if ((rcode = usb_get_conf_descr(dev, len, conf, &buf.conf_desc)))
        return rcode;

    p = &buf;

    // scan through all descriptors
    while (len > 0)
    {
        switch (p->conf_desc.bDescriptorType)
        {
            case USB_DESCRIPTOR_CONFIGURATION:
            case HID_DESCRIPTOR_HID:
            default:
                break;

            case USB_DESCRIPTOR_INTERFACE:
                isHID = (p->iface_desc.bInterfaceClass == USB_CLASS_HID);
                break;

            case USB_DESCRIPTOR_ENDPOINT:
                if (!isHID)
                    break;

                ep_t *ep = (p->ep_desc.bEndpointAddress & 0x80)
                    ? &info->ep_in : &info->ep_out;

                ep->epAddr = (p->ep_desc.bEndpointAddress & 0x0f);
                ep->epType = (p->ep_desc.bmAttributes & EP_TYPE_MSK);
                ep->maxPktSize = p->ep_desc.wMaxPacketSize[0];
                ep->bmNakPower = USB_NAK_NOWAIT;
                ep->epAttribs  = 0;
                break;
        }

        if (!p->conf_desc.bLength || p->conf_desc.bLength > len)
            break;

        // advance to next descriptor
        len -= p->conf_desc.bLength;
        p = (union buf_u*)(p->raw + p->conf_desc.bLength);
    }

    return (info->ep_in.epType == EP_TYPE_INTR && info->ep_out.epType == EP_TYPE_INTR)
        ? 0 : USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
}

static uint8_t mcp_init(
    usb_device_t *dev, usb_device_descriptor_t *dev_desc)
{
    usbrtc_debugf("%s(%d)", __FUNCTION__, dev->bAddress);

    if (dev_desc->bDeviceClass != USB_CLASS_MISC)
        return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;

    if ((dev_desc->idVendor != MCP2221_VID) || (dev_desc->idProduct != MCP2221_PID))
        return USB_ERROR_NO_SUCH_DEVICE;

    uint8_t rcode;
    usb_configuration_descriptor_t conf_desc;

    // Get configuration descriptor
    if ((rcode = usb_get_conf_descr(dev, sizeof(usb_configuration_descriptor_t), 0, &conf_desc))) {
        usbrtc_debugf("mcp2221: failed to get config #0, error #%X", rcode);
        return rcode;
    }

    mcp_rtc_info_t *info = &(dev->mcp_rtc_info);
    ep_t *ep[] = { &info->ep_in, &info->ep_out, NULL };

    // Reset runtime info
    info->chip_type = info->i2c_clock = -1;
    info->read_step = info->last_time = 0;

    for (uint8_t i = 0; ep[i]; i++)
    {
        ep[i]->epAddr = 1;
        ep[i]->epType = 0;
        ep[i]->maxPktSize = 8;
        ep[i]->epAttribs  = 0;
        ep[i]->bmNakPower = USB_NAK_NOWAIT;
    }

    // Parse HID descriptor
    if ((rcode = usb_hid_parse_conf(dev, 0, conf_desc.wTotalLength))) {
        usbrtc_debugf("mcp2221: failed to parse HID config, error #%X", rcode);
        return rcode;
    }

    // Set Configuration Value
    rcode = usb_set_conf(dev, conf_desc.bConfigurationValue);
    if (rcode) usbrtc_debugf("mcp2221: set config error %d", rcode);

    // Request mcp2221 chip info
    if (!mcp_check_status(dev)) {
        usbrtc_debugf("mcp2221: chip not detected");
        return USB_ERROR_NO_SUCH_DEVICE;
    }

    // Probe for rtc chips
    for (uint8_t i = 0; rtc_chips[i]; i++)
    {
        const rtc_chip_t *chip = rtc_chips[i];

        if (!mcp_set_i2c_clock(dev, chip->max_i2c_clock)) {
            usbrtc_debugf("mcp2221: failed to set %dkHz i2c clock rate",
                chip->max_i2c_clock);
            continue;
        }

        if (chip->probe(dev, &mcp_i2c_bus)) {
            iprintf("%s rtc chip found\n", chip->name);
            info->chip_type = i;
            return 0;
        }
    }

    return USB_ERROR_NO_SUCH_DEVICE;
}

static uint8_t mcp_release(usb_device_t *dev)
{
    usbrtc_debugf("%s()", __FUNCTION__);

    return 0;
}

static bool mcp_i2c_bulk_read(
    usb_device_t *dev, uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t length)
{
    usbrtc_debugf("%s()", __FUNCTION__);

    union {
        mcp_i2c_cmd_t cmd;
        mcp_i2c_resp_t resp;
        uint8_t raw[REPORT_SIZE];
    } rpt;

    mcp_rtc_info_t *info = &(dev->mcp_rtc_info);
    uint16_t size;

    if (!buf || !length || length > sizeof(rpt.resp.data)-1)
        return false;

    // state machine
    switch (info->read_step)
    {
        case 1:
        default:
            // set register pointer to 'reg'
            rpt.cmd.cmd_code = CMD_I2C_WR_DATA;
            rpt.cmd.slave_addr = (addr << 1);
            rpt.cmd.size_high = 0;
            rpt.cmd.size_low = 1;
            rpt.cmd.data[0] = reg;

            if (!mcp_exec(dev, rpt.raw, &size))
                return false;

            info->read_step = 2;

        case 2:
            // request to read 'length' byte(s)
            rpt.cmd.cmd_code = CMD_I2C_RD_RPT_START;
            rpt.cmd.slave_addr = (addr << 1) | 1;
            rpt.cmd.size_low = length;
            rpt.cmd.size_high = 0;

            if (!mcp_exec(dev, rpt.raw, &size))
                return false;

            info->read_step = 3;
            usbrtc_debugf("%s: data pending", __FUNCTION__);

            // need delay to complete i2c transaction
            if (length < sizeof(ctime_t)) {
                // chip probing: wait here
                timer_delay_msec(5);
            } else {
                // getting time: wait in main loop
                return false;
            }

        case 3:
            // try to get i2c data
            rpt.cmd.cmd_code = CMD_I2C_GET_DATA;
            rpt.cmd.slave_addr = 0;
            rpt.cmd.size_high = 0;
            rpt.cmd.size_low = 0;

            if (!mcp_exec(dev, rpt.raw, &size))
                return false;

            info->read_step = 1;

            if (rpt.resp.data_size != length) {
                usbrtc_debugf("%s: partial data read", __FUNCTION__);
                return false;
            }

            usbrtc_debugf("%s: data read OK", __FUNCTION__);
            memcpy(buf, &rpt.resp.data, length);
            break;
    }

    return true;
}

static bool mcp_i2c_bulk_write(
    usb_device_t *dev, uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t length)
{
    usbrtc_debugf("%s()", __FUNCTION__);

    union {
        mcp_i2c_cmd_t cmd;
        mcp_i2c_resp_t resp;
        uint8_t raw[REPORT_SIZE];
    } rpt;

    uint16_t size;

    if (!buf || !length || length > sizeof(rpt.cmd.data)-1)
        return false;

    rpt.cmd.cmd_code = CMD_I2C_WR_DATA;
    rpt.cmd.slave_addr = (addr << 1);
    rpt.cmd.size_low = (length + 1);
    rpt.cmd.size_high = 0;

    rpt.cmd.data[0] = reg;
    memcpy(&rpt.cmd.data[1], buf, length);

    return mcp_exec(dev, rpt.raw, &size);
}

static bool mcp_get_time(struct usb_device_entry *dev, ctime_t date)
{
    mcp_rtc_info_t *info = &(dev->mcp_rtc_info);
    const rtc_chip_t *rtc = rtc_chips[info->chip_type];

    // 5ms to comlete i2c transaction
    // 250ms for time polling interval
    uint16_t delay = (info->read_step == 3) ? 5 : 250;

    if (!timer_check(info->last_time, delay))
        return false;

    bool res = rtc->get_time(dev, &mcp_i2c_bus, date);
    info->last_time = timer_get_msec();

    return res;
}

static bool mcp_set_time(struct usb_device_entry *dev, const ctime_t date)
{
    const mcp_rtc_info_t *info = &(dev->mcp_rtc_info);
    const rtc_chip_t *rtc = rtc_chips[info->chip_type];

    return rtc->set_time(dev, &mcp_i2c_bus, date);
}

const usb_rtc_class_config_t usb_rtc_i2c_mcp2221_class = {
    .base = { USB_RTC, mcp_init, mcp_release, NULL },
    .get_time = mcp_get_time,
    .set_time = mcp_set_time,
};
