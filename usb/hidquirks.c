#include "hidquirks.h"
#include <string.h>
#include "user_io.h"
#include "joystick.h"
#include "usb/timer.h"

uint8_t hid_set_report(usb_device_t *, uint8_t iface,
    uint8_t report_type, uint8_t report_id, uint16_t nbytes, uint8_t *);

// Nintendo Pro Controller: wakeup
void procon_wakeup(usb_device_t *dev)
{
    usb_hid_info_t *info = &(dev->hid_info);

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

        timer_delay_msec(20);

        if (usb_out_transfer(dev, &info->iface[0].ep_out, 64, report) != 0)
            break;

        timer_delay_msec(5);

        rpt_size = 64;
        usb_in_transfer(dev, &info->iface[0].ep_in, &rpt_size, report);
    }

    // replace fictive HID report with correct one
    static const hid_report_t report_0x30 = {
        .type = REPORT_TYPE_JOYSTICK,
        .report_id = 0x30,
        .report_size = 64,

        .joystick_mouse = {
            .button_count = 13,

            .button[0] = { .byte_offset = 3, .bitmask = 0x08 }, // A
            .button[1] = { .byte_offset = 3, .bitmask = 0x04 }, // B
            .button[2] = { .byte_offset = 3, .bitmask = 0x40 }, // C (Select)
            .button[3] = { .byte_offset = 4, .bitmask = 0x02 }, // Start
            .button[4] = { .byte_offset = 3, .bitmask = 0x02 }, // X
            .button[5] = { .byte_offset = 3, .bitmask = 0x01 }, // Y
            .button[6] = { .byte_offset = 5, .bitmask = 0x80 }, // L
            .button[7] = { .byte_offset = 3, .bitmask = 0x80 }, // R
            .button[8] = { .byte_offset = 5, .bitmask = 0x40 }, // Z

            // D-Pad
            .button[9]  = { .byte_offset = 5, .bitmask = 0x01 }, // Down
            .button[10] = { .byte_offset = 5, .bitmask = 0x02 }, // Up
            .button[11] = { .byte_offset = 5, .bitmask = 0x04 }, // Right
            .button[12] = { .byte_offset = 5, .bitmask = 0x08 }, // Left

            // L-Stick
            .axis[0] = { // X
                .offset = 48, .size = 12,
                .logical = { .min = 0, .max = 4095 }
            },
            .axis[1] = { // Y
                .offset = 60, .size = 12,
                .logical = { .min = 4095, .max = 0 }
            },

            // R-Stick
            .axis[2] = { // X
                .offset = 72, .size = 12,
                .logical = { .min = 0, .max = 4095 }
            },
            .axis[3] = { // Y
                .offset = 84, .size = 12,
                .logical = { .min = 4095, .max = 0 }
            }
        }
    };

    memcpy(&info->iface[0].conf, &report_0x30, sizeof(hid_report_t));
}

// Logitech K400r: set F1-F12 as primary functions
static void init_logi_K400r(usb_device_t *dev)
{
    hid_set_report(dev, 2, 2, 16, 7, "\x10\x01\x03\x15\x00\x00\x00"); timer_delay_msec(100);
    hid_set_report(dev, 2, 2, 16, 7, "\x10\x01\x0F\x15\x01\x00\x00"); timer_delay_msec(100);
    hid_set_report(dev, 2, 2, 16, 7, "\x10\x01\x10\x15\x00\x00\x00"); timer_delay_msec(100);
}

static void init_5200daptor(usb_device_t *dev)
{
    usb_hid_info_t *info = &(dev->hid_info);
    hid_report_t *conf = &info->iface[0].conf;

    iprintf("hacking 5200daptor\n");

    conf->joystick_mouse.button[2].byte_offset = 4;
    conf->joystick_mouse.button[2].bitmask = 0x40;  // "Reset"
    conf->joystick_mouse.button[3].byte_offset = 4;
    conf->joystick_mouse.button[3].bitmask = 0x10;  // "Start"
}

// special 5200daptor button processing
static void handle_5200daptor(usb_device_t *dev, usb_hid_iface_info_t *iface, uint8_t *buf)
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
        ALIGNED(4) uint8_t buf[6] = { 0,0,0,0,0,0 };
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

static const hid_dev_info_t hid_devs[] = {
    { 0x0079, 0x0006, "Retrolink N64/GC" },
    { 0x0079, 0x0011, "Retrolink NES" },
    { 0x040b, 0x6533, "Speedlink Compet Pro" },
    { 0x0411, 0x00C6, "iBuffalo SFC BSGP801" },
    { 0x045E, 0x028E, "Xbox360 Controller" },
    { 0x046D, 0xC52B, "Unifying Receiver", init_logi_K400r },
    { 0x04D8, 0xF421, "NEOGEO-daptor" },
    { 0x04D8, 0xF672, "Vision-daptor" },
    { 0x04D8, 0xF6EC, "NEOGEO-daptor", init_5200daptor, handle_5200daptor },
    { 0x04D8, 0xF947, "2600-daptor II" },
    { 0x0583, 0x2060, "iBuffalo SFC BSGP801" },
    { 0x0738, 0x2217, "Speedlink Compet Pro" },
    { 0x081F, 0xE401, "SNES Generic Pad" },
    { 0x0F30, 0x1012, "Qanba Q4RAF" },
    { 0x0CA3, 0x0024, "8BitDo M30 2.4G" },
    { 0x057E, 0x2009, "Nintendo Switch Pro", procon_wakeup },
    { 0x057E, 0x200e, "Nintendo Switch JCon", procon_wakeup },
    { 0x1002, 0x9000, "8BitDo FC30" },
    { 0x1235, 0xab11, "8BitDo SFC30" },
    { 0x1235, 0xab21, "8BitDo SFC30"},
    { 0x1F4F, 0x0003, "ROYDS Stick.EX" },
    { 0x1345, 0x1030, "Retro Freak gamepad" },
    { 0x1C59, 0x0026, "Retro Games GAMEPAD" },
};

const hid_dev_info_t* get_hid_dev(uint16_t vid, uint16_t pid)
{
    for (uint32_t n = 0; n < ARRAY_SIZE(hid_devs); n++)
    {
        if (hid_devs[n].vid == vid && hid_devs[n].pid == pid)
        {
            return &hid_devs[n];
        }
    }

    return NULL;
}
