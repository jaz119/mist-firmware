//
// i2c-mcp2221.c
//
// driver for I2C connected RTC via MCP2221 chip
//

#include "rtc/i2c-mcp2221.h"
#include "rtc/pcf85x63.h"
#include "rtc/ds3231.h"
#include "rtc/ds1307.h"
#include "debug.h"

// MCP2221 I2C/UART USB combo
#define MCP2221_VID         0x04d8
#define MCP2221_PID         0x00dd

// HID I2S commands
#define MCP_CMD_STATSET     0x10
#define MCP_CMD_I2CWRITE    0x90
#define MCP_CMD_I2CREAD     0x91

// Status/Set Parameters command struct
typedef struct {
    uint8_t  command_code;          // 0x10 = MCP_CMD_STATUSSET
    uint8_t  unused1;               // Any value
    uint8_t  cancel_i2c;            // 0x10 = Cancel the current I2C transfer
    uint8_t  set_i2c_speed;         // 0x20 = Set the I2C communication speed
    uint8_t  i2c_clock_divider;     // Value of the I2C system clock divider
    uint8_t  unused2[59];           // Any values
} __attribute__ ((packed)) mcp_cmd_status_t;

// Status/Set Parameters response struct
typedef struct {
    uint8_t  command_code;          // 0x10 = MCP_CMD_STATUSSET
    uint8_t  status;                // 0x00 = Command completed successfully
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
    uint8_t  i2c_cur_divider;       // Current I2C speed divider
    uint8_t  i2c_cur_timeout;       // Current I2C timeout value
    uint16_t i2c_address;           // I2C address being used
    uint8_t  unused2[4];            // Don’t care
    uint8_t  scl_input;             // SCL line value, as read from the pin
    uint8_t  sda_input;             // SDA line value, as read from the pin
    uint8_t  intr_edge;             // Interrupt edge detector state, 0 or 1
    uint8_t  i2c_read_pending;      // 0, 1 or 2
    uint8_t  unused3[20];           // Don’t care
    uint8_t  hw_rev_major;          // ‘A’
    uint8_t  hd_rev_minor;          // ‘6’
    uint8_t  fw_rev_major;          // ‘1’
    uint8_t  fw_rev_minor;          // ‘1’
    uint16_t adc_ch0;               // ADC channel 0 input value
    uint16_t adc_ch1;               // ADC channel 1 input value
    uint16_t adc_ch2;               // ADC channel 2 input value
    uint8_t  unused4[];             // Don’t care
} __attribute__ ((packed)) mcp_cmd_response_t;

// Slave I2C command struct
typedef struct {
    uint8_t  command_code;          // I2C command code
    uint8_t  size_low;              // I2C transfer length – 16-bit value – low byte
    uint8_t  size_high;             // I2C transfer length – 16-bit value – high byte
    uint8_t  slave_addr;            // I2C slave address to communicate with
    uint8_t  data_buff[60];         // Data buffer for write
} __attribute__ ((packed)) mcp_i2c_cmd_t;

// Slave I2C response struct
typedef struct {
    uint8_t  command_echo;          // I2C command code echo
    uint8_t  is_failed;             // 0x00 = Completed successfully, 0x01 = Not completed
    uint8_t  internal_state;        // Internal I2C Engine state
    uint8_t  unused[];              // Don’t care
} __attribute__ ((packed)) mcp_i2c_response_t;

static const rtc_chip_t *rtc_chips[] = {
    &rtc_pcf85x63_chip,
    &rtc_ds3231_chip,
    &rtc_ds1307_chip,
};

#define MCP_REQ_OUT (USB_SETUP_HOST_TO_DEVICE|USB_SETUP_TYPE_VENDOR|USB_SETUP_RECIPIENT_DEVICE)
#define MCP_REQ_IN  (USB_SETUP_DEVICE_TO_HOST|USB_SETUP_TYPE_VENDOR|USB_SETUP_RECIPIENT_DEVICE)

static uint8_t usb_hid_parse_conf(
    usb_device_t *dev, uint8_t conf, uint16_t len)
{
    usb_hid_info_t *info = &(dev->hid_info);
    bool isHID = false;
    uint8_t rcode;

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

    /* scan through all descriptors */
    while (len > 0)
    {
        uint8_t i = info->bNumIfaces;

        switch (p->conf_desc.bDescriptorType)
        {
            default:
            case HID_DESCRIPTOR_HID:
            case USB_DESCRIPTOR_CONFIGURATION:
                break;

            case USB_DESCRIPTOR_INTERFACE:
                isHID = (p->iface_desc.bInterfaceClass == USB_CLASS_HID && i < MAX_IFACES);
                if (isHID)
                {
                    info->iface[i].iface_idx = p->iface_desc.bInterfaceNumber;
                    info->iface[i].device_type = HID_DEVICE_UNKNOWN;
                    info->iface[i].conf.type = REPORT_TYPE_NONE;
                }
                break;

            case USB_DESCRIPTOR_ENDPOINT:
                if (isHID)
                {
                    info->iface[i].interval      = p->ep_desc.bInterval;
                    info->iface[i].ep.epAddr     = (p->ep_desc.bEndpointAddress & 0x0F);
                    info->iface[i].ep.epType     = (p->ep_desc.bmAttributes & EP_TYPE_MSK);
                    info->iface[i].ep.maxPktSize = p->ep_desc.wMaxPacketSize[0];
                    info->iface[i].ep.epAttribs  = 0;
                    info->iface[i].ep.bmNakPower = USB_NAK_NOWAIT;
                    info->bNumIfaces++;
                }
                break;
        }

        if (!p->conf_desc.bLength || p->conf_desc.bLength > len)
            break;

        // advance to next descriptor
        len -= p->conf_desc.bLength;
        p = (union buf_u*)(p->raw + p->conf_desc.bLength);
    }

    return 0;
}

static uint8_t mcp_init(
    usb_device_t *dev, usb_device_descriptor_t *dev_desc)
{
    usbrtc_debugf("%s(%d)", __FUNCTION__, dev->bAddress);

    if (dev_desc->bDeviceClass != USB_CLASS_MISC)
        return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;

    if ((dev_desc->idVendor != MCP2221_VID) || (dev_desc->idProduct != MCP2221_PID))
        return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;

    uint8_t rcode = 0;
    usb_configuration_descriptor_t conf_desc;

    // Get configuration descriptor
    if ((rcode = usb_get_conf_descr(dev, sizeof(usb_configuration_descriptor_t), 0, &conf_desc))) {
        usbrtc_debugf("failed to get config descriptor #0");
        return rcode;
    }

    // Reset state
    usb_hid_info_t *info = &(dev->hid_info);
    info->bPollEnable = false;
    info->bNumIfaces = 0;

    for (uint8_t i = 0; i < MAX_IFACES; i++)
    {
        info->iface[i].qLastPollTime = 0;
        info->iface[i].ep.epAddr     = i;
        info->iface[i].ep.epType     = 0;
        info->iface[i].ep.maxPktSize = 8;
        info->iface[i].ep.epAttribs  = 0;
        info->iface[i].ep.bmNakPower = USB_NAK_MAX_POWER;
    }

    // Parse HID descriptor
    if ((rcode = usb_hid_parse_conf(dev, 0, conf_desc.wTotalLength)) != 0)
    {
        usbrtc_debugf("parse HID conf failed");
        return rcode;
    }

    // Check if we found valid HID interfaces
    if (!info->bNumIfaces)
    {
        usbrtc_debugf("no HID interfaces found");
        return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
    }

    // Set Configuration Value
    rcode = usb_set_conf(dev, conf_desc.bConfigurationValue);
    if (rcode) usbrtc_debugf("HID set config error: %d", rcode);

    // TODO
    usbrtc_debugf("!!! init OK");

    return 0;
}

static uint8_t mcp_release(usb_device_t *dev)
{
    usbrtc_debugf("%s()", __FUNCTION__);

    return 0;
}

static uint8_t mcp_i2c_bulk_read(
    usb_device_t *dev, uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t length)
{
    usbrtc_debugf("%s()", __FUNCTION__);

    addr |= 1; // Read operation

    // TODO
    return 0;
}

static uint8_t mcp_i2c_bulk_write(
    usb_device_t *dev, uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t length)
{
    usbrtc_debugf("%s()", __FUNCTION__);

    // TODO
    return 0;
}

const i2c_bus_t mcp_i2c_bus = {
  .bulk_read = mcp_i2c_bulk_read,
  .bulk_write = mcp_i2c_bulk_write
};

static uint8_t mcp_get_time(struct usb_device_entry *dev, timestamp_t date)
{
    usb_hid_info_t *info = &(dev->hid_info);
    const rtc_chip_t *rtc = rtc_chips[info->reserved1];

    usbrtc_debugf("%s: using %s rtc", __FUNCTION__, rtc->name);

    return rtc->get_time(dev, &mcp_i2c_bus, date);
}

static uint8_t mcp_set_time(struct usb_device_entry *dev, timestamp_t date)
{
    usb_hid_info_t *info = &(dev->hid_info);
    const rtc_chip_t *rtc = rtc_chips[info->reserved1];

    usbrtc_debugf("%s: using %s rtc", __FUNCTION__, rtc->name);

    return rtc->set_time(dev, &mcp_i2c_bus, date);
}

const usb_rtc_class_config_t usb_rtc_i2c_mcp2221_class = {
    .class = { USB_RTC, mcp_init, mcp_release, NULL },
    .get_time = mcp_get_time,
    .set_time = mcp_set_time,
};
