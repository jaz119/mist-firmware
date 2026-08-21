#include <string.h>
#include <usb/timer.h>
#include <usb.h>
#include <max3421e.h>
#include <cdc_ecm.h>
#include <debug.h>

#define USB_CDC_SUBCLASS_ACM        0x02
#define USB_CDC_SUBCLASS_ETHERNET   0x06
#define USB_CDC_SUBCLASS_NCM        0x0d
#define USB_CDC_SUBCLASS_MBIM       0x0e

#define USB_CDC_HEADER_TYPE         0x00
#define USB_CDC_UNION_TYPE          0x06
#define USB_CDC_ETHERNET_TYPE       0x0f
#define USB_CDC_NCM_TYPE            0x1a
#define USB_CDC_MBIM_TYPE           0x1b

// Table 62: bits in multicast filter
#define USB_CDC_PACKET_TYPE_DIRECTED    BIT(2)
#define USB_CDC_PACKET_TYPE_BROADCAST   BIT(3)
#define USB_CDC_PACKET_TYPE_MULTICAST   BIT(4)

// CDC header descriptor structure
typedef struct {
    uint8_t bLength;             // Length of this descriptor
    uint8_t bDescriptorType;     // 0x24 (Interface Functional Descriptor)
    uint8_t bDescriptorSubtype;  // Type of CDC header

    union {
        struct {
            uint8_t bMasterInterface;
            uint8_t bSlaveInterface;
        } unio; // 0x06 (Union Functional Descriptor)

        struct {
            uint8_t  iMACAddress;
            uint32_t bmEthernetStatistics;
            uint16_t wMaxSegmentSize;
            uint16_t wNumberMCFilters;
            uint8_t  bNumberPowerFilters;
        } eth;  // 0x0F (Ethernet Networking Functional Descriptor)
    };
} __attribute__((packed)) usb_cdc_header_t;

// Basic CDC notification structure
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bNotification;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_cdc_notification_t;

#define USB_CDC_NOTIF_NETWORK_CONNECTION        0x00
#define USB_CDC_NOTIF_RESPONSE_AVAILABLE        0x01
#define USB_CDC_NOTIF_CONNECTION_SPEED_CHANGE   0x2a

static uint8_t hex_to_int(uint16_t ch, bool *is_ok)
{
    if (ch >= '0' && ch <= '9')
        return (ch - '0');
    if (ch >= 'A' && ch <= 'F')
        return (ch - 'A' + 0xA);
    if (ch >= 'a' && ch <= 'f')
        return (ch - 'a' + 0xA);

    *is_ok = false;
    return 0xff;
}

static bool decode_mac(
    const usb_string_descriptor_t *desc, net_mac_t mac)
{
    if (desc->bLength < 26 || desc->bDescriptorType != USB_DESCRIPTOR_STRING)
        return false;

    bool is_ok = true;

    for (uint8_t n = 0, i = 0; n < 12; n += 2, i++)
    {
        uint8_t hi = hex_to_int(desc->wString[n], &is_ok);
        uint8_t lo = hex_to_int(desc->wString[n + 1], &is_ok);

        mac[i] = (hi << 4) | lo;

        if (!is_ok)
            return false;
    }

    return true;
}

static uint8_t usb_set_cdc_eth_filter(
    usb_device_t *dev, uint16_t comm_iface_num, uint16_t filt_mask)
{
    return usb_ctrl_req(
        dev, USB_REQ_CL_SET_INTF, USB_REQUEST_SET_ETH_PACKET_FILTER,
        filt_mask & 0xff, filt_mask >> 8, comm_iface_num, 0x0000, NULL);
}

static uint8_t usb_ecm_parse_conf(
    usb_device_t *dev, uint8_t confValue, uint16_t len)
{
    usb_cdc_ecm_info_t *info = &(dev->ecm_info);
    uint8_t rcode, is_cap_printed = 0, iface_num = 0xff;

    if (len > USB_MAX_CONFIG_DESC_SIZE)
        return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;

    union buf_u {
        usb_configuration_descriptor_t conf_desc;
        usb_interface_descriptor_t iface_desc;
        usb_endpoint_descriptor_t ep_desc;
        usb_cdc_header_t cdc_desc;
        uint8_t raw[len];
    } buf, *p;

    union {
        usb_string0_descriptor_t str0_desc;
        usb_string_descriptor_t str_desc;
        uint8_t buf[64];
    } str;

    // get full size descriptor
    if ((rcode = usb_get_conf_descr(dev, len, confValue, &buf.conf_desc)))
        return rcode;

    p = &buf;

    // scan through all descriptors
    while (len > 0)
    {
        switch (p->conf_desc.bDescriptorType)
        {
            case USB_DESCRIPTOR_CONFIGURATION:
                break;

            case USB_DESCRIPTOR_INTERFACE:
                if (p->iface_desc.bInterfaceClass == USB_CLASS_COM_AND_CDC_CTRL
                    && p->iface_desc.bInterfaceSubClass == USB_CDC_SUBCLASS_ETHERNET)
                {
                    iface_num = p->iface_desc.bInterfaceNumber;
                }
                else if (p->iface_desc.bInterfaceClass == USB_CLASS_CDC_DATA
                        && p->iface_desc.bNumEndpoints == 2)
                {
                    iface_num = p->iface_desc.bInterfaceNumber;
                    info->alt_setting = p->iface_desc.bAlternateSetting;
                    info->data_iface_num = iface_num;
                }
                else
                {
                    iface_num = 0xff;
                }

                is_cap_printed = false;
                break;

            case USB_DESCRIPTOR_CS_INTERFACE:
                if (p->cdc_desc.bDescriptorSubtype == USB_CDC_UNION_TYPE)
                    info->comm_iface_num = p->cdc_desc.unio.bMasterInterface;
                else if (p->cdc_desc.bDescriptorSubtype == USB_CDC_ETHERNET_TYPE) {
                    // get MAC address string
                    if (usb_get_string_descr(dev, sizeof(str), p->cdc_desc.eth.iMACAddress, 0, &str.str_desc) != 0)
                        break;
                    // convert into MAC
                    if (!decode_mac(&str.str_desc, info->mac))
                        break;
                } else if (p->cdc_desc.bDescriptorSubtype == USB_CDC_NCM_TYPE)
                    return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
                else if (p->cdc_desc.bDescriptorSubtype == USB_CDC_MBIM_TYPE)
                    return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
                break;

            case USB_DESCRIPTOR_ENDPOINT:
            {
                if (iface_num == 0xff)
                    break;

                ep_t *ep = NULL;
                bool is_in = (p->ep_desc.bEndpointAddress & 0x80);
                uint8_t ep_type = (p->ep_desc.bmAttributes & EP_TYPE_MSK);

                if (ep_type == EP_TYPE_BULK && iface_num == info->data_iface_num) {
                    ep = is_in ? &info->ep_in : &info->ep_out;
                } else if (ep_type == EP_TYPE_INTR && is_in) {
                    ep = &info->ep_int;
                }

                if (!ep || ep->addr)
                    break;

                if (!is_cap_printed) {
                    iprintf("CDC-ECM Interface %d:\n", iface_num);
                    is_cap_printed = true;
                }

                ep->nakPower   = USB_NAK_NOWAIT;
                ep->addr       = (p->ep_desc.bEndpointAddress & 0x0f);
                ep->type       = ep_type;
                ep->maxPktSize = (p->ep_desc.wMaxPacketSize[1] << 8) | p->ep_desc.wMaxPacketSize[0];
                ep->interval   = p->ep_desc.bInterval;

                iprintf(" -> %s endpoint %d, packet size: %d\n",
                    (is_in) ? "IN" : "OUT", ep->addr, ep->maxPktSize);
                break;
            }
        }

        if (!p->conf_desc.bLength || p->conf_desc.bLength > len)
            break;

        // advance to next descriptor
        len -= p->conf_desc.bLength;
        p = (union buf_u*)(p->raw + p->conf_desc.bLength);
    }

    return (info->ep_in.type == EP_TYPE_BULK && info->ep_out.type == EP_TYPE_BULK
        && info->ep_int.type == EP_TYPE_INTR) ? 0 : USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
}

static uint8_t usb_ecm_init(
    usb_device_t *dev, usb_device_descriptor_t *dev_desc)
{
    if (dev_desc->bDeviceClass != USB_CLASS_USE_CLASS_INFO
        && dev_desc->bDeviceClass != USB_CLASS_COM_AND_CDC_CTRL
        && dev_desc->bDeviceClass != USB_CLASS_MISC)
    {
        return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
    }

    ALIGNED(4) union {
        usb_configuration_descriptor_t conf_desc;
    } buf;

    uint8_t rcode;
    usb_cdc_ecm_info_t *info = &(dev->ecm_info);

    // Reset runtime info
    memset(info, 0, sizeof(usb_cdc_ecm_info_t));

    // Find CDC-ECM config
    for (uint8_t n = 0; n < dev_desc->bNumConfigurations; n++)
    {
        if ((rcode = usb_get_conf_descr(dev, sizeof(usb_configuration_descriptor_t), n, &buf.conf_desc))) {
            errorf("cdc_ecm: failed to get config%d, error 0x%02x", n, rcode);
            return rcode;
        }

        // Try to parse CDC-ECM config
        if ((rcode = usb_ecm_parse_conf(dev, n, buf.conf_desc.wTotalLength)) == 0)
            break;

        buf.conf_desc.bConfigurationValue = 0xff;
    }

    if (buf.conf_desc.bConfigurationValue == 0xff)
        return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;

    // Set Configuration Value
    rcode = usb_set_conf(dev, buf.conf_desc.bConfigurationValue);
    if (rcode) {
        errorf("cdc_ecm: set config%d, error 0x%02x",
            buf.conf_desc.bConfigurationValue, rcode);
        return rcode;
    }

    // Activate Network Interface
    rcode = usb_set_interface(dev, info->data_iface_num, info->alt_setting);
    if (rcode) {
        errorf("cdc_ecm: set interface (%d:%d), error 0x%02x",
            info->data_iface_num, info->alt_setting, rcode);
        return rcode;
    }

    // Set Packet Filter
    rcode = usb_set_cdc_eth_filter(dev, info->comm_iface_num,
        USB_CDC_PACKET_TYPE_DIRECTED | USB_CDC_PACKET_TYPE_BROADCAST);
    if (rcode) {
        errorf("cdc_ecm: set packet filter, error 0x%02x", rcode);
        return rcode;
    }

    infof("cdc_ecm: MAC %02x:%02x:%02x:%02x:%02x:%02x",
        info->mac[0], info->mac[1], info->mac[2],
        info->mac[3], info->mac[4], info->mac[5]);

    return 0;
}

static uint8_t usb_ecm_poll(usb_device_t *dev)
{
    usb_cdc_ecm_info_t *info = &(dev->ecm_info);

    // poll INT endpoint
    if (!timer_check(info->last_poll, info->ep_int.interval))
        return 0;

    ALIGNED(4) uint8_t buf[16];
    uint16_t read = sizeof(buf);

    uint8_t rcode = usb_in_transfer(dev, &(info->ep_int), &read, buf);

    if (rcode)
    {
        if (rcode != hrNAK)
            errorf("%s(%d): error 0x%02x",
                __FUNCTION__, dev->bAddress, rcode);
    }
    else if (read >= sizeof(usb_cdc_notification_t))
    {
        static uint8_t prev_link_state = 0xff;
        const usb_cdc_notification_t *event = (usb_cdc_notification_t *) buf;

        if (event->bNotification == USB_CDC_NOTIF_NETWORK_CONNECTION)
        {
            info->link_is_up = !!(event->wValue);

            if (prev_link_state != info->link_is_up) {
                warningf("cdc_ecm: link is %s", info->link_is_up ? "UP" : "DOWN");
                prev_link_state = info->link_is_up;
            }
        }
    }

    info->last_poll = timer_get_msec();
    return 0;
}

static uint8_t usb_ecm_release(usb_device_t *)
{
    return 0;
}

static bool ecm_link_is_up(usb_device_t *dev)
{
    const usb_cdc_ecm_info_t *info = &(dev->ecm_info);
    return info->link_is_up;
}

static const uint8_t *ecm_get_mac(usb_device_t *dev)
{
    const usb_cdc_ecm_info_t *info = &(dev->ecm_info);
    return info->mac;
}

static void ecm_send_pkt(
    usb_device_t *dev, net_pkt_cb receive_tx_frame, uint16_t tx_size)
{
    usb_cdc_ecm_info_t *info = &(dev->ecm_info);
    ALIGNED(4) static unsigned char tx_buf[MAX_FRAME_LEN];

    if (!info->link_is_up || tx_size > MAX_FRAME_LEN)
        return;

    // fetch data from fpga
    receive_tx_frame(tx_buf, tx_size);

    // xfer via BULK endpoint
    usb_out_transfer(dev, &(info->ep_out), tx_size, tx_buf);
}

static void ecm_recv_pkt(usb_device_t *dev, net_pkt_cb send_rx_frame)
{
    usb_cdc_ecm_info_t *info = &(dev->ecm_info);
    ALIGNED(4) static unsigned char rx_buf[MAX_FRAME_LEN];
    static uint16_t rx_count = 0;

    if (!info->link_is_up || rx_count >= MAX_FRAME_LEN) {
        rx_count = 0;
        return;
    }

    const uint16_t max_pkt = info->ep_in.maxPktSize;
    uint16_t read = MIN(max_pkt, MAX_FRAME_LEN - rx_count);

    // poll BULK endpoint
    uint8_t rcode = usb_in_transfer(
        dev, &(info->ep_in), &read, rx_buf + rx_count);

    if (rcode)
    {
        if (rcode != hrNAK) {
            errorf("%s(%d): error 0x%02x",
                __FUNCTION__, dev->bAddress, rcode);
            rx_count = 0;
        }
        return;
    }

    if (read > 0)
        rx_count += read;

    if (read == max_pkt && rx_count < MAX_FRAME_LEN)
        return; // frame part

    if (rx_count >= ETH_HLEN)
    {
        // frame is full
        uint16_t eth_type = (rx_buf[12] << 8) | rx_buf[13];

        if (eth_type == ETH_P_IP || eth_type == ETH_P_ARP) {
            // send it to fpga
            send_rx_frame(rx_buf, MAX(64, rx_count));
        }
    }

    rx_count = 0;
}

const usb_nic_class_config_t usb_cdc_ecm_class = {
    .base = { USB_NIC, usb_ecm_init, usb_ecm_release, usb_ecm_poll },
    .link_is_up = ecm_link_is_up,
    .send_pkt = ecm_send_pkt,
    .recv_pkt = ecm_recv_pkt,
    .get_mac = ecm_get_mac,
};
