#include "user_io.h"
#include <string.h>
#include "hidquirks.h"
#include "joystick.h"
#include "usb/timer.h"
#include "state.h"
#include "debug.h"

#define INIT_PKT(_vid, _pid, _data) \
{ \
    .vid    = (_vid),  \
    .pid    = (_pid),  \
    .data   = (_data), \
    .len    = ARRAY_SIZE(_data), \
}

// Init packet for specific device
typedef struct {
    uint16_t vid;
    uint16_t pid;
    const uint8_t *data;
    uint8_t len;
} init_packet_t;

uint8_t hid_set_report(usb_device_t *, uint8_t iface,
    uint8_t report_type, uint8_t report_id, uint16_t nbytes, uint8_t *);

// Xbox One controller: check interface descriptor
static bool xone_check_iface(const usb_interface_descriptor_t *iface)
{
    return (iface->bInterfaceClass == USB_CLASS_VENDOR_SPECIFIC)
        && (iface->bInterfaceSubClass == 0x47)
        && (iface->bInterfaceProtocol == 0xD0)
        && (iface->bInterfaceNumber   == 0x00);
}

// Xbox One controller: init
static bool xone_init(usb_device_t *dev)
{
    usb_hid_iface_info_t* iface = &dev->hid_info.iface[0];

#define GIP_CMD_POWER    0x05
    #define GIP_PWR_ON   0x00
#define GIP_CMD_AUTH     0x06
#define GIP_CMD_LED      0x0A
    #define GIP_LED_ON   0x01
#define GIP_CMD_INPUT    0x20

#define GIP_SEQ0         0x00
#define GIP_OPT_INTERNAL 0x20
#define GIP_PL_LEN(N)    (N)

    static const uint8_t xone_power_on[] = {
        GIP_CMD_POWER, GIP_OPT_INTERNAL, GIP_SEQ0, GIP_PL_LEN(1), GIP_PWR_ON
    };
    static const uint8_t xone_led_on[] = {
        GIP_CMD_LED, GIP_OPT_INTERNAL, GIP_SEQ0, GIP_PL_LEN(3), 0x00, GIP_LED_ON, 0x14
    };
    static const uint8_t xone_auth_done[] = {
        GIP_CMD_AUTH, GIP_OPT_INTERNAL, GIP_SEQ0, GIP_PL_LEN(2), 0x01, 0x00
    };

    // wakeup commands sequense
    static const init_packet_t xone_wakeup[] = {
        INIT_PKT(0x0000, 0x0000, xone_power_on),
        INIT_PKT(0x0000, 0x0000, xone_led_on),
        INIT_PKT(0x0000, 0x0000, xone_auth_done),
    };

    ALIGNED(4) uint8_t report[64], seq = 0;
    memset(report, 0, sizeof(report));

    for (int n = 0; n < ARRAY_SIZE(xone_wakeup); n++)
    {
        const init_packet_t *pkt = &xone_wakeup[n];

        if (pkt->vid && pkt->vid != dev->vid) continue;
        if (pkt->pid && pkt->pid != dev->pid) continue;

        memcpy(report, pkt->data, pkt->len);
        report[2] = seq++; // GIP_SEQ

        uint16_t rpt_size = sizeof(report);
        timer_delay_msec(iface->ep_out.interval);

        uint8_t rcode = usb_out_transfer(dev, &iface->ep_out, sizeof(report), report);
        if (rcode) {
            hid_debugf("%s: error 0x%02x", __FUNCTION__, rcode);
            return false;
        }

        usb_in_transfer(dev, &iface->ep_in, &rpt_size, report);
    }

    static const hid_report_t xone_report = {
        .type = REPORT_TYPE_JOYSTICK,
        .report_id = GIP_CMD_INPUT,
        .report_size = 0x20,

        .joystick_mouse = {
            .button_count = 16,

            .button[0]  = { .byte_offset = 4, .bitmask = BIT(4) }, // A
            .button[1]  = { .byte_offset = 4, .bitmask = BIT(5) }, // B
            .button[2]  = { .byte_offset = 4, .bitmask = BIT(3) }, // View (Select)
            .button[3]  = { .byte_offset = 4, .bitmask = BIT(2) }, // Menu (Start)
            .button[4]  = { .byte_offset = 4, .bitmask = BIT(6) }, // X
            .button[5]  = { .byte_offset = 4, .bitmask = BIT(7) }, // Y
            .button[6]  = { .byte_offset = 5, .bitmask = BIT(4) }, // LB
            .button[7]  = { .byte_offset = 5, .bitmask = BIT(5) }, // RB
            .button[8]  = { .byte_offset = 5, .bitmask = BIT(6) }, // L3
            .button[9]  = { .byte_offset = 5, .bitmask = BIT(7) }, // R3
            .button[10] = { .byte_offset = 4, .bitmask = BIT(0) }, // Sync
            .button[11] = { .byte_offset = 4, .bitmask = BIT(1) }, // Guide

            // D-Pad
            .button[12] = { .byte_offset = 5, .bitmask = BIT(0) }, // Up
            .button[13] = { .byte_offset = 5, .bitmask = BIT(1) }, // Down
            .button[14] = { .byte_offset = 5, .bitmask = BIT(2) }, // Left
            .button[15] = { .byte_offset = 5, .bitmask = BIT(3) }, // Right

            .axis = {
                { .offset = 80,  .size = 16, .logical = {.min = -32768, .max = 32767} }, // LX
                { .offset = 96,  .size = 16, .logical = {.min = 32767, .max = -32768} }, // LY

                { .offset = 112, .size = 16, .logical = {.min = -32768, .max = 32767} }, // RX
                { .offset = 128, .size = 16, .logical = {.min = 32767, .max = -32768} }, // RY
            }
        }
    };

    memcpy(&iface->conf, &xone_report, sizeof(hid_report_t));
    return true;
}

// Xbox One controller: MENU key polling
FORCE_ARM static void xone_poll(usb_device_t *, usb_hid_iface_info_t *iface, uint8_t *buf)
{
    const hid_button_t *guide = &iface->conf.joystick_mouse.button[11];

    StateJoySetMenu(
        buf[guide->byte_offset] & guide->bitmask,
        joystick_index(iface->jindex));
}

// Xbox360 controller: check interface descriptor
static bool x360_check_iface(const usb_interface_descriptor_t *iface)
{
    return (iface->bInterfaceClass == USB_CLASS_VENDOR_SPECIFIC)
        && (iface->bInterfaceSubClass == 0x5D)
        && (iface->bInterfaceProtocol == 0x01);
}

// Xbox360 controller: init
static bool x360_init(usb_device_t *dev)
{
    usb_hid_iface_info_t* iface = &dev->hid_info.iface[0];

    // LED command: top-left blink, then on
    ALIGNED(4) static const uint8_t led_on[] = {
        0x01, 0x03, 0x02
    };

    uint8_t rcode = usb_out_transfer(dev, &iface->ep_out, sizeof(led_on), led_on);
    if (rcode) {
        hid_debugf("%s: error 0x%02x", __FUNCTION__, rcode);
        return false;
    }

    // actual HID report
    // buttons mapping is native for 8BitDo M30
    static const hid_report_t x360_report = {
        .type = REPORT_TYPE_JOYSTICK,
        .report_id = 0x00,
        .report_size = 0x14,

        .joystick_mouse = {
            .button_count = 16,

            .button[0] = { .byte_offset = 3, .bitmask = BIT(4) }, // A
            .button[1] = { .byte_offset = 3, .bitmask = BIT(5) }, // B
            .button[2] = { .byte_offset = 5, .bitmask = BIT(7) }, // C (Select)
            .button[3] = { .byte_offset = 2, .bitmask = BIT(4) }, // Start
            .button[4] = { .byte_offset = 3, .bitmask = BIT(6) }, // X
            .button[5] = { .byte_offset = 3, .bitmask = BIT(7) }, // Y
            .button[6] = { .byte_offset = 3, .bitmask = BIT(0) }, // L
            .button[7] = { .byte_offset = 4, .bitmask = BIT(7) }, // R
            .button[8] = { .byte_offset = 3, .bitmask = BIT(1) }, // Z
            .button[9] = { .byte_offset = 2, .bitmask = BIT(7) }, // R3
            .button[10]= { .byte_offset = 2, .bitmask = BIT(5) }, // Minus
            .button[11]= { .byte_offset = 3, .bitmask = BIT(2) }, // Guide

            // D-Pad
            .button[12] = { .byte_offset = 2, .bitmask = BIT(0) }, // Up
            .button[13] = { .byte_offset = 2, .bitmask = BIT(1) }, // Down
            .button[14] = { .byte_offset = 2, .bitmask = BIT(2) }, // Left
            .button[15] = { .byte_offset = 2, .bitmask = BIT(3) }, // Right

            .axis = {
                { .offset = 48, .size = 16, .logical = {.min = -32768, .max = 32767} }, // LX
                { .offset = 64, .size = 16, .logical = {.min = 32767, .max = -32768} }, // LY

                { .offset = 80, .size = 16, .logical = {.min = -32768, .max = 32767} }, // RX
                { .offset = 96, .size = 16, .logical = {.min = 32767, .max = -32768} }, // RY
            }
        }
    };

    memcpy(&iface->conf, &x360_report, sizeof(hid_report_t));
    return true;
}

// Xbox360 controller: MENU key polling
FORCE_ARM static void x360_poll(usb_device_t *, usb_hid_iface_info_t *iface, uint8_t *buf)
{
    const hid_button_t *guide = &iface->conf.joystick_mouse.button[11];

    StateJoySetMenu(
        buf[guide->byte_offset] & guide->bitmask,
        joystick_index(iface->jindex));
}

// Nintendo Pro Controller: wakeup
static bool procon_init(usb_device_t *dev)
{
    usb_hid_iface_info_t* iface = &dev->hid_info.iface[0];

#define JC_OUTPUT_RUMBLE_AND_SUBCMD     0x01
    #define JC_SUBCMD_SET_REPORT_MODE   0x03
    #define JC_SUBCMD_ENABLE_IMU        0x40

#define JC_OUTPUT_USB_CMD               0x80
    #define JC_USB_CMD_CONN_STATUS      0x01
    #define JC_USB_CMD_HANDSHAKE        0x02
    #define JC_USB_CMD_BAUDRATE_3M      0x03
    #define JC_USB_CMD_NO_TIMEOUT       0x04

    static const uint8_t cmds[][2] = {
        { JC_OUTPUT_USB_CMD, JC_USB_CMD_BAUDRATE_3M },
        { JC_OUTPUT_USB_CMD, JC_USB_CMD_HANDSHAKE  },
        { JC_OUTPUT_USB_CMD, JC_USB_CMD_NO_TIMEOUT },
    };

    uint16_t rpt_size = 64;
    ALIGNED(4) uint8_t report[64];
    memset(report, 0, 64);

    for (int n = 0; n < ARRAY_SIZE(cmds); n++)
    {
        report[0] = cmds[n][0];
        report[1] = cmds[n][1];

        rpt_size = 64;
        timer_delay_msec(iface->ep_out.interval);

        uint8_t rcode = usb_out_transfer(dev, &iface->ep_out, 64, report);
        if (rcode) {
            hid_debugf("%s: error 0x%02x", __FUNCTION__, rcode);
            return false;
        }

        usb_in_transfer(dev, &iface->ep_in, &rpt_size, report);
    }

    // actual HID report
    // buttons mapping is native for 8BitDo M30
    static const hid_report_t report_0x30 = {
        .type = REPORT_TYPE_JOYSTICK,
        .report_id = 0x30,
        .report_size = 64,

        .joystick_mouse = {
            .button_count = 16,

            .button[0] = { .byte_offset = 3, .bitmask = BIT(3) }, // A
            .button[1] = { .byte_offset = 3, .bitmask = BIT(2) }, // B
            .button[2] = { .byte_offset = 3, .bitmask = BIT(6) }, // C (Select)
            .button[3] = { .byte_offset = 4, .bitmask = BIT(1) }, // Start
            .button[4] = { .byte_offset = 3, .bitmask = BIT(1) }, // X
            .button[5] = { .byte_offset = 3, .bitmask = BIT(0) }, // Y
            .button[6] = { .byte_offset = 5, .bitmask = BIT(7) }, // L
            .button[7] = { .byte_offset = 3, .bitmask = BIT(7) }, // R
            .button[8] = { .byte_offset = 5, .bitmask = BIT(6) }, // Z
            .button[9] = { .byte_offset = 4, .bitmask = BIT(4) }, // Home
            .button[10]= { .byte_offset = 4, .bitmask = BIT(7) }, // ZR
            .button[11]= { .byte_offset = 4, .bitmask = BIT(0) }, // Minus

            // D-Pad
            .button[12] = { .byte_offset = 5, .bitmask = BIT(0) }, // Down
            .button[13] = { .byte_offset = 5, .bitmask = BIT(1) }, // Up
            .button[14] = { .byte_offset = 5, .bitmask = BIT(2) }, // Right
            .button[15] = { .byte_offset = 5, .bitmask = BIT(3) }, // Left

            .axis = {
                { .offset = 48, .size = 12, .logical = { .min = 0, .max = 4095 } }, // LX
                { .offset = 60, .size = 12, .logical = { .min = 4095, .max = 0 } }, // LY

                { .offset = 72, .size = 12, .logical = { .min = 0, .max = 4095 } }, // RX
                { .offset = 84, .size = 12, .logical = { .min = 4095, .max = 0 } }, // RY
            }
        }
    };

    memcpy(&iface->conf, &report_0x30, sizeof(hid_report_t));
    return true;
}

// Nintendo Pro Controller: MENU key polling
FORCE_ARM static void procon_poll(usb_device_t *, usb_hid_iface_info_t *iface, uint8_t *buf)
{
    const hid_button_t *home = &iface->conf.joystick_mouse.button[9];

    StateJoySetMenu(
        buf[home->byte_offset] & home->bitmask,
        joystick_index(iface->jindex));
}

// Logitech K400r: set F1-F12 as primary functions
static bool logi_K400r_init(usb_device_t *dev)
{
    const static char cmds[][7] = {
        "\x10\x01\x03\x15\x00\x00\x00", // enable HID++
        "\x10\x01\x0F\x15\x01\x00\x00", // swap F-keys
        "\x10\x01\x10\x15\x00\x00\x00", // Fn-lock
    };

    for (uint32_t n = 0; n < ARRAY_SIZE(cmds); n++)
    {
        timer_delay_msec(20);
        hid_set_report(dev, 2, 2, 16, sizeof(cmds[n]), (uint8_t *)cmds[n]);
    }

    return true;
}

static bool init_5200daptor(usb_device_t *dev)
{
    hid_report_t *conf = &dev->hid_info.iface[0].conf;

    hid_button_t *reset = &conf->joystick_mouse.button[2];
    hid_button_t *start = &conf->joystick_mouse.button[3];

    reset->byte_offset = 4;
    reset->bitmask = BIT(6);

    start->byte_offset = 4;
    start->bitmask = BIT(4);

    return true;
}

// special 5200daptor button processing
static void poll_5200daptor(usb_device_t *dev, usb_hid_iface_info_t *iface, uint8_t *buf)
{
    // list of buttons that are reported as keys
    static const struct {
        uint8_t byte_offset;   // offset of the byte within the report which the button bit is in
        uint8_t mask;          // bitmask of the button bit
        uint8_t key_code[2];   // usb keycodes to be sent for joystick 0 and joystick 1
    } button_map[] ALIGNED(4) = {
        { 4, 0x10, { 0x3a, 0x3d }}, /* START -> f1/f4 */
        { 4, 0x20, { 0x3b, 0x3e }}, /* PAUSE -> f2/f5 */
        { 4, 0x40, { 0x3c, 0x3f }}, /* RESET -> f3/f6 */
        { 5, 0x01, { 0x1e, 0x21 }}, /*     1 ->  1/4  */
        { 5, 0x02, { 0x1f, 0x22 }}, /*     2 ->  2/5  */
        { 5, 0x04, { 0x20, 0x23 }}, /*     3 ->  3/6  */
        { 5, 0x08, { 0x14, 0x15 }}, /*     4 ->  q/r  */
        { 5, 0x10, { 0x1a, 0x17 }}, /*     5 ->  w/t  */
        { 5, 0x20, { 0x08, 0x1c }}, /*     6 ->  e/y  */
        { 5, 0x40, { 0x04, 0x09 }}, /*     7 ->  a/f  */
        { 5, 0x80, { 0x16, 0x0a }}, /*     8 ->  s/g  */
        { 6, 0x01, { 0x07, 0x0b }}, /*     9 ->  d/h  */
        { 6, 0x02, { 0x1d, 0x19 }}, /*     * ->  z/v  */
        { 6, 0x04, { 0x1b, 0x05 }}, /*     0 ->  x/b  */
        { 6, 0x08, { 0x06, 0x11 }}, /*     # ->  c/n  */
        { 0, 0x00, { 0x00, 0x00 }}  /* ----  end ---- */
    };

    // keyboard events are only generated for the first
    // two joysticks in the system
    uint8_t jindex = joystick_index(iface->jindex);
    if (jindex > 1) return;

    // build map of pressed keys
    uint16_t keys = 0;
    for (uint32_t i=0; button_map[i].mask; i++)
        if (buf[button_map[i].byte_offset] & button_map[i].mask)
            keys |= (1<<i);

    // check if keys have changed
    if (iface->key_state != keys) {
        uint8_t buf[6] = { 0,0,0,0,0,0 };
        uint8_t p = 0;

        // report up to 6 pressed keys
        for (uint32_t i=0; (i<16) && (p<6); i++)
            if (keys & (1<<i))
                buf[p++] = button_map[i].key_code[jindex];

        // generate key events
        user_io_kbd(0x00, buf, UIO_PRIORITY_GAMEPAD, dev->vid, dev->pid);

        // save current state of keys
        iface->key_state = keys;
    }
}

ALIGNED(4) static const hid_dev_info_t hid_devs[] = {
    { 0x045E, 0x028E, "Xbox 360 Controller", x360_init, x360_poll, x360_check_iface },
    { 0x045E, 0x028F, "Xbox 360 Controller", x360_init, x360_poll, x360_check_iface },
    { 0x045E, 0x02D1, "Xbox One Controller", xone_init, xone_poll, xone_check_iface },
    { 0x045E, 0x02DD, "Xbox One Controller", xone_init, xone_poll, xone_check_iface },
    { 0x045E, 0x0B12, "Xbox S|X Controller", xone_init, xone_poll, xone_check_iface },
    { 0x057E, 0x2009, "Nintendo Switch Pro", procon_init, procon_poll },
    { 0x057E, 0x200E, "Nintendo Switch Joy-Con", procon_init, procon_poll },
    { 0x0E6F, 0x0133, "Xbox 360 Controller", x360_init, x360_poll, x360_check_iface },
    { 0x0E6F, 0x0139, "PDP Afterglow Prismatic", xone_init, xone_poll, xone_check_iface },
    { 0x0E6F, 0x013A, "Xbox One Controller", xone_init, xone_poll, xone_check_iface },
    { 0x0E6F, 0x0161, "Xbox One Controller", xone_init, xone_poll, xone_check_iface },
    { 0x0E6F, 0x0162, "Xbox One Controller", xone_init, xone_poll, xone_check_iface },
    { 0x0E6F, 0x0163, "Xbox One Controller", xone_init, xone_poll, xone_check_iface },
    { 0x0E6F, 0x0213, "Xbox 360 Controller", x360_init, x360_poll, x360_check_iface },
    { 0x0E6F, 0x0246, "PDP Rock Candy", xone_init, xone_poll, xone_check_iface },
    { 0x0E6F, 0x021F, "Xbox 360 Controller", x360_init, x360_poll, x360_check_iface },
    { 0x0E6F, 0x02A0, "Xbox One Controller", xone_init, xone_poll, xone_check_iface },
    { 0x0E6F, 0x02A1, "Xbox One Controller", xone_init, xone_poll, xone_check_iface },
    { 0x0E6F, 0x02AB, "Xbox One Controller", xone_init, xone_poll, xone_check_iface },
    { 0x0E6F, 0x0401, "Xbox 360 Controller", x360_init, x360_poll, x360_check_iface },
    { 0x1532, 0x0A57, "Razer Wolverine V3 Pro", x360_init, x360_poll, x360_check_iface },
    { 0x1532, 0x0A59, "Razer Wolverine V3 Pro", x360_init, x360_poll, x360_check_iface },
    { 0x162E, 0xBEEF, "Xbox 360 Controller", x360_init, x360_poll, x360_check_iface },
    { 0x17EF, 0x6182, "Lenovo Legion Controller", x360_init, x360_poll, x360_check_iface },
    { 0x1BAD, 0xF016, "Xbox 360 Controller", x360_init, x360_poll, x360_check_iface },
    { 0x1BAD, 0xFD00, "Razer Onza TE", x360_init, x360_poll, x360_check_iface },
    { 0x1BAD, 0xFD01, "Razer Onza", x360_init, x360_poll, x360_check_iface },
    { 0x054C, 0x05C4, "Sony DualShock 4" },
    { 0x054C, 0x09CC, "Sony DualShock 4" },
    { 0x054C, 0x0CE6, "Sony DualSense" },
    { 0x0CA3, 0x0024, "8BitDo M30 2.4g" },
    { 0x1002, 0x9000, "8BitDo FC30" },
    { 0x1235, 0xAB11, "8BitDo SFC30" },
    { 0x1235, 0xAB21, "8BitDo SFC30"},
    { 0x2DC8, 0x6001, "8BitDo SN30 Pro" },
    { 0x040B, 0x6533, "Competition Pro" },
    { 0x0738, 0x2217, "Competition Pro" },
    { 0x046D, 0xC52B, "Unifying Receiver", logi_K400r_init },
    { 0x04D8, 0xF6EC, "NEOGEO-daptor", init_5200daptor, poll_5200daptor },
    { 0x04D8, 0xF421, "NEOGEO-daptor" },
    { 0x04D8, 0xF672, "Vision-daptor" },
    { 0x04D8, 0xF947, "2600-daptor II" },
    { 0x0411, 0x00C6, "iBuffalo SFC BSGP801" },
    { 0x0583, 0x2060, "iBuffalo SFC BSGP801" },
    { 0x081F, 0xE401, "SNES Generic Pad" },
    { 0x0F30, 0x1012, "Qanba Q4RAF" },
    { 0x1F4F, 0x0003, "ROYDS Stick.EX" },
    { 0x0079, 0x0006, "Retrolink N64/GC" },
    { 0x0079, 0x0011, "Retrolink NES" },
    { 0x1345, 0x1030, "Retro Freak gamepad" },
    { 0x1C59, 0x0026, "Retro Games gamepad" },
};

FAST const hid_dev_info_t* get_hid_dev(uint16_t vid, uint16_t pid)
{
    const hid_dev_info_t *it = hid_devs;
    const hid_dev_info_t *end = &hid_devs[ARRAY_SIZE(hid_devs)];

    for (; it < end; it++)
    {
        if (it->vid == vid && it->pid == pid)
        {
            return it;
        }
    }

    return NULL;
}
