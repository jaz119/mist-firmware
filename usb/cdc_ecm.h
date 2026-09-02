#ifndef USB_CDC_ECM_H
#define USB_CDC_ECM_H

/*
 * CDC-ECM driver
 * for many USB NIC dongles
 */

#include <stdbool.h>
#include <inttypes.h>

typedef uint8_t net_mac_t[6];
typedef struct usb_device_entry usb_device_t;

#define ETH_HLEN        14
#define ETH_MIN_FLEN    60
#define ETH_MAX_FLEN    1536

#define ETH_P_IP        0x0800
#define ETH_P_ARP       0x0806

// usb cdc_ecm device context
typedef struct {
    ep_t ep_in;
    ep_t ep_out;

    ep_t ep_int;
    uint32_t last_poll;
    uint16_t rx_count;

    net_mac_t mac;
    uint8_t link_is_up;

    uint8_t comm_iface_num;
    uint8_t data_iface_num;
    uint8_t alt_setting;
} usb_cdc_ecm_info_t;

typedef void (*net_pkt_cb)(uint8_t *, uint16_t);

// nic driver iface
typedef struct {
    usb_device_class_config_t base;
    bool (*link_is_up)(usb_device_t *);
    void (*send_pkt)(usb_device_t *, net_pkt_cb, uint16_t);
    void (*recv_pkt)(usb_device_t *, net_pkt_cb);
    const uint8_t* (*get_mac)(usb_device_t *);
} usb_nic_class_config_t;

extern const usb_nic_class_config_t usb_cdc_ecm_class;

#endif // USB_CDC_ECM_H
