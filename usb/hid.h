#ifndef HID_H
#define HID_H

#include <stdbool.h>
#include <inttypes.h>
#include "hidparser.h"

#define HID_LED_NUM_LOCK            0x01
#define HID_LED_CAPS_LOCK           0x02
#define HID_LED_SCROLL_LOCK         0x04

/* HID constants. Not part of chapter 9 */
/* Class-Specific Requests */
#define HID_REQUEST_GET_REPORT      0x01
#define HID_REQUEST_GET_IDLE        0x02
#define HID_REQUEST_GET_PROTOCOL    0x03
#define HID_REQUEST_SET_REPORT      0x09
#define HID_REQUEST_SET_IDLE        0x0A
#define HID_REQUEST_SET_PROTOCOL    0x0B

#define HID_DESCRIPTOR_HID          0x21
#define HID_DESCRIPTOR_REPORT       0x22
#define HID_DESRIPTOR_PHY           0x23

/* Protocol Selection */
#define HID_BOOT_PROTOCOL           0x00
#define HID_RPT_PROTOCOL            0x01

/* HID Interface Class SubClass Codes */
#define HID_BOOT_INTF_SUBCLASS      0x01

#define HID_PROTOCOL_NONE           0x00
#define HID_PROTOCOL_KEYBOARD       0x01
#define HID_PROTOCOL_MOUSE          0x02

#define HID_REQ_HIDREPORT     (USB_SETUP_DEVICE_TO_HOST | USB_SETUP_TYPE_STANDARD | USB_SETUP_RECIPIENT_INTERFACE)
#define HID_REQ_HIDOUT        (USB_SETUP_HOST_TO_DEVICE | USB_SETUP_TYPE_CLASS | USB_SETUP_RECIPIENT_INTERFACE)
#define HID_REQ_HIDIN         (USB_SETUP_DEVICE_TO_HOST | USB_SETUP_TYPE_CLASS | USB_SETUP_RECIPIENT_INTERFACE)

#define MAX_IFACES            3  // max supported interfaces per device
#define REPORT_BUF_SZ         64

#define HID_DEVICE_UNKNOWN    0
#define HID_DEVICE_MOUSE      1
#define HID_DEVICE_KEYBOARD   2
#define HID_DEVICE_JOYSTICK   3

// when the joystick axis counts as trigger a direction for binary
#define JOYSTICK_AXIS_MIN           0
#define JOYSTICK_AXIS_MID           128
#define JOYSTICK_AXIS_MAX           255
#define JOYSTICK_AXIS_TRIGGER_MIN   64
#define JOYSTICK_AXIS_TRIGGER_MAX   192

typedef struct {
  ep_t ep_in;
  ep_t ep_out;

  uint32_t lastPollTime;      // last poll time

  union {
    struct {
      int16_t rem[MAX_AXES];  // mouse: position remainder
      uint8_t is_alive;       // mouse: is alive
    };
    uint32_t jmap;            // joystick: last reported state
    uint16_t key_state;       // 5200daptor: last reported key state
  };

  uint8_t iface_idx;
  uint8_t device_type;        // device type
  uint8_t jindex;             // joystick index

  uint8_t has_boot_mode: 1;   // device supports Boot mode
  uint8_t ignore_boot_mode: 1; // don't use Boot mode even if device supports it
  hid_report_t conf;          // HID report struct

} usb_hid_iface_info_t;

typedef struct hid_dev_info_t hid_dev_info_t;

typedef struct {
  bool pollEnable;            // poll enable flag
  const hid_dev_info_t *quirks; // quirks info
  uint8_t  numIfaces;         // ifaces count
  usb_hid_iface_info_t iface[MAX_IFACES];
} usb_hid_info_t;

/* HID descriptor */
typedef struct  {
  uint8_t  bLength;
  uint8_t  bDescriptorType;
  uint16_t bcdHID;            // HID class specification release
  uint8_t  bCountryCode;
  uint8_t  bNumDescriptors;   // Number of additional class specific descriptors
  uint8_t  bDescrType;        // Type of class descriptor
  uint8_t  wDescriptorLength[2]; // Total size of the Report descriptor
} __attribute__((packed)) usb_hid_descriptor_t;

// interface to usb core
extern const usb_device_class_config_t usb_hid_class;

uint8_t hid_get_joysticks(void);
unsigned char get_keyboards(void);
unsigned char get_mice(void);

void hid_set_kbd_led(unsigned char led, bool on);

// HID low-level remapping - do not confuse with virtual joystick in joymapping.h
void hid_joystick_button_remap_init(void);
char hid_joystick_button_remap(char *, char, int);

void joy_key_map_init(void); // older function, prefer to use joymapping.h function

#endif // HID_H
