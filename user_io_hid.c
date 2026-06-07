#include <stdlib.h>
#include <string.h>

#include <user_io.h>
#include <user_io_hid.h>
#include <minimig/core.h>
#include <8bit/core.h>
#include <usb/usb.h>
#include <usb/hid.h>
#include <usb/joystick.h>
#include <usb/joymapping.h>
#include <keycodes.h>
#include <archie.h>
#include <mist_cfg.h>
#include <state.h>
#include <menu.h>
#include <debug.h>

int kbd_reset = 0;

// avoid multiple keyboard/controllers to interfere
// keyboard = 0, joypad with key mappings = 1
static uint8_t latest_kbd_priority = 0;

// up to 16 key can be remapped
#define MAX_REMAP  16
uint8_t key_remap_table[MAX_REMAP][2];

// keep state of caps lock
static bool caps_lock_toggle = 0;
static bool caps_status = 0;
static bool scrl_status = 0;
static bool num_status = 0;

static uint8_t modifier = 0, pressed[6] = { 0,0,0,0,0,0 };

uint8_t ps2_typematic_rate = 0x80;
static uint32_t ps2_typematic_timer;

// keyboard LEDs control
#define KBD_LED_CAPS_CONTROL    BIT(0)
#define KBD_LED_CAPS_STATUS     BIT(1)
#define KBD_LED_CAPS_MASK       ( KBD_LED_CAPS_CONTROL | KBD_LED_CAPS_STATUS )
#define KBD_LED_NUM_CONTROL     BIT(2)
#define KBD_LED_NUM_STATUS      BIT(3)
#define KBD_LED_NUM_MASK        ( KBD_LED_NUM_CONTROL | KBD_LED_NUM_STATUS )
#define KBD_LED_SCRL_CONTROL    BIT(4)
#define KBD_LED_SCRL_STATUS     BIT(5)
#define KBD_LED_SCRL_MASK       ( KBD_LED_SCRL_CONTROL | KBD_LED_SCRL_STATUS )
#define KBD_LED_FLAG_STATUS     BIT(6)
#define KBD_LED_FLAG_MASK       ( BIT(7) | KBD_LED_FLAG_STATUS )

static uint32_t keyboard_leds = 0;
static uint32_t kbd_led_timer;

// 1000 / (2 ^ (39 - rate) ^ (1 / 8))
static const short ps2_typematic_rates[] = {
    34, 37, 40, 44, 48, 52, 57, 62, 68, 74, 81, 88, 96, 105, 114, 125, 136,
    148, 162, 176, 192, 210, 229, 250, 272, 297, 324, 353, 385, 420, 458, 500
};

typedef enum {
    PS2_KBD_IDLE,
    PS2_KBD_SCAN_GETSET,
    PS2_KBD_TYPEMATIC_SET,
    PS2_KBD_LED_SET
} ps2_kbd_state_t;

int ps2_kbd_scan_set = 2;
static ps2_kbd_state_t ps2_kbd_state;

// mouse position
// for storage rate limitation
#define X   0
#define Y   1
#define Z   2
int32_t mouse_pos[2][3] = { {0, 0, 0}, {0, 0, 0} };
uint32_t mouse_flags[2] = { 0, 0 };
uint32_t mouse_timer;

typedef enum {
    PS2_MOUSE_IDLE,
    PS2_MOUSE_SETRESOLUTION,
    PS2_MOUSE_SETSAMPLERATE
} ps2_mouse_state_t;

static ps2_mouse_state_t ps2_mouse_state;

static uint32_t ps2_mouse_status;
static uint32_t ps2_mouse_resolution;
static uint32_t ps2_mouse_samplerate;

static bool autofire;
static int autofire_joy;
static uint32_t autofire_timer;
static uint32_t autofire_mask;
static uint32_t autofire_map;

uint16_t keycode_ps2(uint8_t key)
{
    //  replace MENU key by RGUI e.g.
    // to allow using RGUI on reduced keyboards
    // without physical key (it also disables the use of Menu for OSD)
    if (mist_cfg.key_menu_as_rgui && mist_cfg.keyrah_mode == 0 && key == 0x65)
    {
        return EXT | 0x27;
    }

    return (ps2_kbd_scan_set == 1) ? usb2ps2_set1[key] : usb2ps2[key];
}

uint16_t modify_keycode_ps2(uint8_t key)
{
    static const uint16_t ps2_modifier[] = {
        0x14, 0x12, 0x11, (EXT | 0x1f), (EXT | 0x14), 0x59, (EXT | 0x11), (EXT | 0x27)
    };

    static const uint16_t ps2_modifier_set1[] = {
        0x1d, 0x2a, 0x38, MISS, (EXT | 0x1d), 0x36, (EXT | 0x38), MISS
    };

    return (ps2_kbd_scan_set == 1) ? ps2_modifier_set1[key] : ps2_modifier[key];
}

#define BREAK   0x8000

void send_keycode_ps2(uint16_t code)
{
    // send ps2 keycodes for those cores that prefer ps2
    spi_uio_cmd_cont(UIO_KEYBOARD);

    // "pause" has a complex code
    if ((code & 0xff) == 0x77)
    {
        // pause does not have a break code
        if (!(code & BREAK))
        {
            // Pause key sends E11477E1F014E077
            static const uint8_t c[] = {
                0xe1, 0x14, 0x77, 0xe1, 0xf0, 0x14, 0xf0, 0x77
            };

            for (int i = 0; i < ARRAY_SIZE(c); i++) {
                spi8(c[i]);
            }
        }
    }
    else
    {
        // prepend extended code flag if required
        if (code & EXT)
            spi8(0xe0);

        if (code & BREAK)
        {
            // prepend break code if required
            if (ps2_kbd_scan_set == 1) {
                code |= 0x80;
            } else {
                spi8(0xf0);
            }
        }

        spi8(code & 0xff);  // send code itself
    }

    DisableIO();
}

void add_modifiers(uint8_t mod, uint16_t *keys_ps2)
{
    uint8_t offset = 1;
    uint32_t index = 0;

    // translates a USB modifiers into scancodes
    while (offset)
    {
        if ((mod & offset) && core && core->modify_keycode)
        {
            uint16_t ps2_value = core->modify_keycode(index);

            if (ps2_value != MISS)
            {
                if (ps2_value & EXT)
                    ps2_value = (0xE000 | (ps2_value & 0xFF));

                for (int i = 0; i < 6; i++)
                {
                    if (keys_ps2[i] == 0)
                    {
                        keys_ps2[i] = ps2_value;
                        break;
                    }
                }
            }
        }

        offset <<= 1;
        index++;
    }
}

void handle_kbd_commands_ps2()
{
    spi_uio_cmd_cont(UIO_KEYBOARD_IN);
    uint8_t c = spi_in();
    uint8_t cmd = spi_in();
    DisableIO();

    if (c == UIO_KEYBOARD_IN)
    {
        // receiving echo of the command code
        // shows the core supports this message
        debugf("PS2 keyboard cmd: %02x", cmd);

        switch (ps2_kbd_state)
        {
            case PS2_KBD_IDLE:
                switch (cmd)
                {
                    case 0xFF: // reset
                        ps2_kbd_scan_set = 2;
                        spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
                        spi_uio_cmd8(UIO_KEYBOARD, 0xAA); // BAT successful
                        break;
                    case 0xF2: // read ID
                        spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
                        spi_uio_cmd8(UIO_KEYBOARD, 0xAB); // ID1
                        spi_uio_cmd8(UIO_KEYBOARD, 0x83); // ID2
                        break;
                    case 0xF0: // scan get/set
                        spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
                        ps2_kbd_state = PS2_KBD_SCAN_GETSET;
                        break;
                    case 0xF3: // typematic set
                        spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
                        ps2_kbd_state = PS2_KBD_TYPEMATIC_SET;
                        break;
                    case 0xED: // set LEDs
                        spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
                        ps2_kbd_state = PS2_KBD_LED_SET;
                        break;
                    case 0xEE: // echo
                        spi_uio_cmd8(UIO_KEYBOARD, 0xEE); // ACK
                        break;
                    case 0xF4: // enable scanning
                        // TODO: handle the message
                        spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
                        break;
                    case 0xF5: // disable scanning
                        // TODO: handle the message
                        spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
                        break;
                    case 0xF6: // set default parameters
                        ps2_kbd_scan_set = 2;
                        spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
                        break;
                }
                break;
            case PS2_KBD_SCAN_GETSET:
                if (cmd <= 3) {
                    spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
                    if (!cmd) {
                        // get
                        spi_uio_cmd8(UIO_KEYBOARD, ps2_kbd_scan_set);
                    } else {
                        // set
                        ps2_kbd_scan_set = cmd;
                    }
                    ps2_kbd_state = PS2_KBD_IDLE;
                } else {
                    spi_uio_cmd8(UIO_KEYBOARD, 0xFE); // RESEND
                }
                break;
            case PS2_KBD_TYPEMATIC_SET:
                ps2_typematic_rate = cmd;
                spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
                ps2_kbd_state = PS2_KBD_IDLE;
                break;
            case PS2_KBD_LED_SET:
                // TODO: handle the message
                spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
                ps2_kbd_state = PS2_KBD_IDLE;
                break;
        }
    }
}

void handle_typematic_repeat_ps2()
{
    if (ps2_typematic_rate & 0x80)
        return;

    if (ps2_kbd_state != PS2_KBD_IDLE)
        return;

    if (CheckTimer(ps2_typematic_timer))
    {
        ps2_typematic_timer = GetTimer(ps2_typematic_rates[ps2_typematic_rate & 0x1f]);

        for (int i = 5; i >= 0; i--)
        {
            if (pressed[i] && core && core->keycode && core->send_keycode)
            {
                uint16_t code = core->keycode(pressed[i]);

                if (!osd_is_visible && !(code & CAPS_LOCK_TOGGLE) && !(code & NUM_LOCK_TOGGLE))
                    core->send_keycode(code);
                break;
            }
        }
    }
}

char user_io_key_remap(const char *s, char action, int tag)
{
    if (action == INI_SAVE)
        return 0;

    // s is a string containing two comma separated hex numbers
    if ((strlen(s) != 5) && (s[2] != ','))
    {
        ini_parser_debugf("malformed entry %s", s);
        return 0;
    }

    for (int i = 0; i < MAX_REMAP; i++)
    {
        if (!key_remap_table[i][0])
        {
            key_remap_table[i][0] = strtol(s, NULL, 16);
            key_remap_table[i][1] = strtol(s + 3, NULL, 16);

            ini_parser_debugf("key_remap entry %d = %02x,%02x",
                i, key_remap_table[i][0], key_remap_table[i][1]);
            return 0;
        }
    }

    ini_parser_debugf("key_remap table is full");
    return 0;
}

static bool key_used_by_osd(uint16_t s)
{
    // this key is only used to open the OSD and has no keycode
    if ((s & OSD_OPEN) && !(s & 0xff))
        return true;

    // no keys are suppressed if the OSD is inactive
    if (!osd_is_visible)
        return false;

    // in atari mode eat all keys if the OSD is online,
    // else none as it's up to the core to forward keys to the OSD
    return ((core_type == CORE_TYPE_MISTERY)
            || (core_type == CORE_TYPE_ARCHIE)
            || (core_type == CORE_TYPE_8BIT));
}

void user_io_kbd(uint8_t m, uint8_t *k, uint8_t priority)
{
    // ignore lower priority clears if higher priority key was pressed
    if (m == 0 && !(k[0] | k[1] | k[2] | k[3] | k[4] | k[5]))
    {
        if (priority > latest_kbd_priority) {
            // lower number = higher priority
            return;
        }
    }

    // set for next call
    latest_kbd_priority = priority;
    uint32_t reset_m = m;

    for (int i = 0; i < 6; i++)
    {
        if (k[i] == 0x4c) {
            reset_m |= 0x100;
        }
    }

    user_io_check_reset(reset_m, mist_cfg.reset_combo);

    uint8_t keycodes[6];
    uint16_t keycodes_ps2[6];

    // remap keycodes if requested
    for (int i = 0; (i < 6) && k[i]; i++)
    {
        for (int j = 0; j < MAX_REMAP; j++)
        {
            if (key_remap_table[j][0] == k[i])
            {
                k[i] = key_remap_table[j][1];
                break;
            }
        }
    }

    if (!core || !core->keycode || !core->send_keycode)
        return;

    // handle modifier keys
    if ((m != modifier) && !osd_is_visible && core->modify_keycode)
    {
        for (int i = 0; i < 8; i++)
        {
            // Do we have a downstroke on a modifier key?
            if ((m & BIT(i)) && !(modifier & BIT(i)))
            {
                if (core->modify_keycode(i) != MISS)
                    core->send_keycode(core->modify_keycode(i));
            }

            if (!(m & BIT(i)) && (modifier & BIT(i)))
            {
                if (core->modify_keycode(i) != MISS)
                    core->send_keycode(BREAK | core->modify_keycode(i));
            }
        }

        modifier = m;
    }

    // check if there are keys in the pressed list
    // which aren't reported anymore
    for (int i = 0; i < 6; i++)
    {
        uint16_t code = core->keycode(pressed[i]);

        if (pressed[i] && (code != MISS))
        {
            debugf("key 0x%X break: 0x%X", pressed[i], code);

            int j;
            for (j = 0; j < 6 && pressed[i] != k[j]; j++);

            // don't send break for caps lock
            if (j == 6)
            {
                // If OSD is visible, then all keys are sent into the OSD
                // using Amiga key codes since the OSD itself uses Amiga key codes
                // for historical reasons. If the OSD is invisble then only
                // those keys marked for OSD in the core specific table are
                // sent for OSD handling.
                if (code & OSD_OPEN)
                {
                    OsdKeySet(0x80 | KEY_MENU);
                }
                else if (osd_is_visible)
                {
                    // special OSD key handled internally
                    OsdKeySet(0x80 | minimig_keycode(pressed[i]));
                }

                if (!key_used_by_osd(code))
                {
                    // key is not used by OSD
                    if (!(code & CAPS_LOCK_TOGGLE) && !(code & NUM_LOCK_TOGGLE))
                    {
                        core->send_keycode(BREAK | code);
                    }
                }
            }
        }
    }

    for (int i = 0; i < 6; i++)
    {
        uint16_t code = core->keycode(k[i]);

        if (k[i] && (k[i] <= KEYCODE_MAX) && (code != MISS))
        {
            int j;
            // check if this key is already in the list of pressed keys
            for (j = 0; j < 6 && k[i] != pressed[j]; j++);

            if (j == 6)
            {
                debugf("key 0x%X make: 0x%X", k[i], code);

                // If OSD is visible, then all keys are sent into the OSD
                // using Amiga key codes since the OSD itself uses Amiga key codes
                // for historical reasons. If the OSD is invisble then only
                // those keys marked for OSD in the core specific table are
                // sent for OSD handling.
                if (code & OSD_OPEN)
                {
                    OsdKeySet(KEY_MENU);
                }
                else
                {
                    if (osd_is_visible)
                    {
                        // special OSD key handled internally
                        OsdKeySet(minimig_keycode(k[i]));
                    }
                    else if (((mist_cfg.joystick_autofire_combo == 0 && k[i] == 0x62) // KP0
                                || (mist_cfg.joystick_autofire_combo == 1 && k[i] == 0x2B)) // TAB
                            && (m & 0x05) == 0x05 // LCTR+LALT
                            && (core_type == CORE_TYPE_8BIT
                                || core_type == CORE_TYPE_ARCHIE
                                || core_type == CORE_TYPE_MISTERY))
                    {
                        autofire = ((autofire + 1) & 0x03);
                        InfoMessage(config_autofire_msg[autofire]);
                    }
                }

                // no further processing of any key
                // that is currently redirected to the OSD
                if (!key_used_by_osd(code))
                {
                    // key is not used by OSD
                    if (code & CAPS_LOCK_TOGGLE)
                    {
                        // send alternating make and break codes for caps lock
                        core->send_keycode((code & 0xff) | (caps_lock_toggle ? BREAK : 0));
                        caps_lock_toggle ^= 1;
                        hid_set_kbd_led(HID_LED_CAPS_LOCK, caps_lock_toggle);
                    }
                    else
                    {
                        core->send_keycode(code);
                    }
                }
            }
        }
    }

    for (int i = 0; i < 6; i++)
    {
        pressed[i] = k[i];

        // send raw USB code
        keycodes[i] = pressed[i];
        keycodes_ps2[i] = core->keycode(pressed[i]);
    }

    StateKeyboardSet(m, keycodes, keycodes_ps2);

    // set the typematic timer to the first delay
    if (core_type == CORE_TYPE_8BIT)
    {
        ps2_typematic_timer = GetTimer((((ps2_typematic_rate & 0x60) >> 5) + 1) * 250);
    }
}

void handle_mouse_events_ps2()
{
    // frequently check ps2 mouse for events
    if (!CheckTimer(mouse_timer))
        return;

    mouse_timer = GetTimer(MOUSE_FREQ);

    for (int idx = 0; idx < 2; idx++)
    {
        if (!(mouse_flags[idx] & 0x08))
            continue;

        // has ps2 mouse data been updated in the meantime
        uint8_t ps2_mouse[4];

        // PS2 format:
        // YOvfl, XOvfl, dy8, dx8, 1, mbtn, rbtn, lbtn
        // dx[7:0]
        // dy[7:0]
        // 0,0,btn5,btn,dz[3:0]
        ps2_mouse[0] = mouse_flags[idx];

        // ------ X axis -----------
        // store sign bit in first byte
        ps2_mouse[0] |= (mouse_pos[idx][X] < 0) ? 0x10 : 0x00;
        if (mouse_pos[idx][X] < -255) {
            // min possible value + overflow flag
            ps2_mouse[0] |= 0x40;
            ps2_mouse[1] = -128;
        } else if (mouse_pos[idx][X] > 255) {
            // max possible value + overflow flag
            ps2_mouse[0] |= 0x40;
            ps2_mouse[1] = 255;
        } else
            ps2_mouse[1] = mouse_pos[idx][X];

        // ------ Y axis -----------
        // store sign bit in first byte
        ps2_mouse[0] |= (mouse_pos[idx][Y] < 0) ? 0x20 : 0x00;
        if (mouse_pos[idx][Y] < -255) {
            // min possible value + overflow flag
            ps2_mouse[0] |= 0x80;
            ps2_mouse[2] = -128;
        } else if (mouse_pos[idx][Y] > 255) {
            // max possible value + overflow flag
            ps2_mouse[0] |= 0x80;
            ps2_mouse[2] = 255;
        } else
            ps2_mouse[2] = mouse_pos[idx][Y];

        // ------ Z axis -----------
        ps2_mouse[3] = 0;
        if (mouse_pos[idx][Z] < -8) {
            // min possible value
            ps2_mouse[3] = -8;
        } else if (mouse_pos[idx][Z] > 7) {
            // max possible value
            ps2_mouse[3] = 7;
        } else
            ps2_mouse[3] = mouse_pos[idx][Z];

        // collect movement info and send at predefined rate
        if (!(ps2_mouse[0] == 0x08 && ps2_mouse[1] == 0 && ps2_mouse[2] == 0 && ps2_mouse[3] == 0))
        {
            debugf("PS2 MOUSE(%d): 0x%x %d %d %d",
                idx, ps2_mouse[0], ps2_mouse[1], ps2_mouse[2], ps2_mouse[3]);
        }

        // old message sends the movements for all mice
        spi_uio_cmd_cont(UIO_MOUSE);
        spi8(ps2_mouse[0]);
        spi8(ps2_mouse[1]);
        spi8(ps2_mouse[2]);
        DisableIO();

        // new message with Intellimouse PS2 message
        spi_uio_cmd_cont(UIO_MOUSE0_EXT + idx);
        spi8(ps2_mouse[0]);
        spi8(ps2_mouse[1]);
        spi8(ps2_mouse[2]);
        spi8(ps2_mouse[3]);
        DisableIO();

        // reset counters
        mouse_flags[idx] = 0;
        mouse_pos[idx][X] = mouse_pos[idx][Y] = mouse_pos[idx][Z] = 0;
    }
}

void handle_mouse_commands_ps2()
{
    spi_uio_cmd_cont(UIO_MOUSE_IN);
    uint8_t c = spi_in();
    uint8_t cmd = spi_in();
    DisableIO();

    if (c == UIO_MOUSE_IN)
    {
        // receiving echo of the command code shows the core supports this message
        debugf("PS2 mouse cmd: 0x%02x", cmd);

        switch (ps2_mouse_state)
        {
            case PS2_MOUSE_IDLE:
                switch (cmd)
                {
                    case 0xFF: // reset
                        spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
                        spi_uio_cmd8(UIO_MOUSE0_EXT, 0xAA); // BAT successful
                        spi_uio_cmd8(UIO_MOUSE0_EXT, 0);
                        break;
                    case 0xF6: // set defaults
                        spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
                        break;
                    case 0xE6: // set mouse scaling to 1:1
                        spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
                        ps2_mouse_status &= ~0x10;
                        break;
                    case 0xE7: // set mouse scaling to 1:2
                        spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
                        ps2_mouse_status |= 0x10;
                        break;
                    case 0xE8: // set resolution
                        spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
                        ps2_mouse_state = PS2_MOUSE_SETRESOLUTION;
                        break;
                    case 0xE9: // status request
                        spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
                        spi_uio_cmd8(UIO_MOUSE0_EXT, ps2_mouse_status);
                        spi_uio_cmd8(UIO_MOUSE0_EXT, ps2_mouse_resolution);
                        spi_uio_cmd8(UIO_MOUSE0_EXT, ps2_mouse_samplerate);
                        break;
                    case 0xF2: // get device ID
                        spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
                        spi_uio_cmd8(UIO_MOUSE0_EXT, 0x00); // Normal PS2 mouse
                        break;
                    case 0xF4: // enable data reporting
                        spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
                        ps2_mouse_status |= 0x20;
                        break;
                    case 0xF5: // disable data reporting
                        spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
                        ps2_mouse_status &= ~0x20;
                        break;
                    case 0xF3: // set sample rate
                        spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
                        ps2_mouse_state = PS2_MOUSE_SETSAMPLERATE;
                        break;
                }
                break;
            case PS2_MOUSE_SETRESOLUTION:
                spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
                ps2_mouse_resolution = cmd;
                ps2_mouse_state = PS2_MOUSE_IDLE;
                break;
            case PS2_MOUSE_SETSAMPLERATE:
                spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
                ps2_mouse_samplerate = cmd;
                ps2_mouse_state = PS2_MOUSE_IDLE;
                break;
        }
    }
}

void send_mouse_ps2(uint8_t idx, uint8_t b, int8_t x, int8_t y, int8_t z)
{
    // 8bit core expects ps2 like data
    mouse_pos[idx][X] += x;
    mouse_pos[idx][Y] -= y; // ps2 y axis is reversed over usb
    mouse_pos[idx][Z] += z;
    mouse_flags[idx] |= (0x08 | (b & 7));
}

uint8_t user_io_swap_joystick(uint8_t joy)
{
    // swap joystick 0 and 1
    // since 1 is the one used primarily on most systems
    if (joy < 2 && (!mist_cfg.joystick_disable_swap || user_io_core_type() == CORE_TYPE_8BIT))
    {
        joy ^= 1;
    }

    // if real DB9 mouse is preffered, switch the id back to 1
    if (joy == 0 && mist_cfg.joystick0_prefer_db9)
    {
        return 1;
    }

    return joy;
}

static inline void user_io_digital_joystick_legacy(uint8_t joy, uint8_t map)
{
    // every other core else uses this
    // (even MIST, joystick 3 and 4 were introduced later)
    spi_uio_cmd8((joy < 2) ? (UIO_JOYSTICK0 + joy) : (UIO_JOYSTICK2 + joy - 2), map);
}

void send_digital_joystick(uint8_t joy, uint32_t map)
{
    // "only" 6 joysticks are supported
    if (joy > 5)
        return;

    if (osd_is_visible && map)
        return;

    // legacy api
    user_io_digital_joystick_legacy(joy, map & 0xff);

    // actual api
    spi_uio_cmd32(UIO_JOYSTICK0_EXT + joy, map & 0xfffff);

    // autofire setup
    if (autofire && (map & 0x30))
    {
        autofire_mask = map & 0x30;
        autofire_map = (autofire_map & autofire_mask) | (map & ~autofire_mask);

        if (autofire_joy != joy)
        {
            autofire_joy = joy;
            autofire_timer = GetTimer(autofire * 50);
        }
    }
    else
    {
        autofire_joy = -1;
    }
}

void send_analog_joystick(uint8_t joy, int LX, int LY, int RX, int RY)
{
    if (osd_is_visible)
        return;

    spi_uio_cmd8_cont(UIO_ASTICK, joy);
    spi8(LX);
    spi8(LY);
    spi8(RX);
    spi8(RY);
    DisableIO();
}

void user_io_mouse(uint8_t idx, uint8_t b, int8_t x, int8_t y, int8_t z)
{
    if (core && core->send_mouse)
    {
        core->send_mouse(idx, b, x, y, z);
    }
}

void user_io_digital_joystick(uint8_t joy, uint32_t map)
{
    if (core && core->send_digital_joy)
    {
        core->send_digital_joy(joy, map);
    }
}

void user_io_analog_joystick(uint8_t joy, int LX, int LY, int RX, int RY)
{
    if (core && core->send_analog_joy)
    {
        core->send_analog_joy(joy, LX, LY, RX, RY);
    }
}

static inline char dig2ana(bool min, bool max)
{
    if (min && !max) return -128;
    if (max && !min) return  127;
    return 0;
}

static void user_io_joystick(unsigned char joy, uint16_t map)
{
    // digital joysticks also send analog signals
    user_io_digital_joystick(joy, map);

    user_io_analog_joystick(joy,
        dig2ana(map & JOY_LEFT, map & JOY_RIGHT),
        dig2ana(map & JOY_UP,   map & JOY_DOWN), 0 ,0);
}

static void set_kbd_led(unsigned char led, bool on)
{
    if (led & HID_LED_CAPS_LOCK)
    {
        if (!(keyboard_leds & KBD_LED_CAPS_CONTROL))
            hid_set_kbd_led(led, on);

        caps_status = on;
    }

    if (led & HID_LED_NUM_LOCK)
    {
        if (!(keyboard_leds & KBD_LED_NUM_CONTROL))
            hid_set_kbd_led(led, on);

        num_status = on;
    }

    if (led & HID_LED_SCROLL_LOCK)
    {
        if (!(keyboard_leds & KBD_LED_SCRL_CONTROL))
            hid_set_kbd_led(led, on);

        scrl_status = on;
    }
}

// read 8 bit keyboard LEDs status from FPGA
static uint8_t user_io_kbdled_get_status()
{
    spi_uio_cmd_cont(UIO_GET_KBD_LED);
    uint8_t c = spi_in();
    DisableIO();
    return c;
}

void user_io_hid_poll()
{
    // poll db9 joysticks
    uint16_t joy_state = 0, joy_map = 0;

    if (GetDB9(0, &joy_state))
    {
        joy_map = virtual_joystick_mapping(0x00db, 0x0000, joy_state, NULL);

        uint8_t idx = joystick_renumber(0);
        uint8_t id = mist_cfg.joystick_db9_fixed_index ? idx : joystick_count();

        if (!user_io_osd_is_visible())
            user_io_joystick(idx, joy_map);

        StateUsbIdSet(0x00db, 0x0000, 2, id);
        StateJoySet(joy_map, id); // send to OSD
        StateJoySetExtra(joy_map >> 8, id); // send to OSD
        StateUsbJoySet(joy_state, joy_state >> 8, id);
        virtual_joystick_keyboard(joy_map);
    }

    if (GetDB9(1, &joy_state))
    {
        joy_map = virtual_joystick_mapping(0x00db, 0x0001, joy_state, NULL);

        uint8_t idx = joystick_renumber(1);
        uint8_t id = mist_cfg.joystick_db9_fixed_index ? idx : joystick_count() + 1;

        if (!user_io_osd_is_visible())
            user_io_joystick(idx, joy_map);

        StateUsbIdSet(0x00db, 0x0001, 2, id);
        StateJoySet(joy_map, id); // send to OSD
        StateJoySetExtra(joy_map >> 8, id); // send to OSD
        StateUsbJoySet(joy_state, joy_state >> 8, id);
        virtual_joystick_keyboard(joy_map);
    }

    if (autofire && autofire_joy >= 0 && autofire_joy <= 5 && CheckTimer(autofire_timer))
    {
        autofire_map ^= autofire_mask;
        spi_uio_cmd32(UIO_JOYSTICK0_EXT + autofire_joy, autofire_map & 0xfffff);
        autofire_timer = GetTimer(autofire * 50);
    }

    if (core && core->poll)
    {
        core->poll();
    }

    if (CheckTimer(kbd_led_timer))
    {
        kbd_led_timer = GetTimer(KBD_LED_FREQ);
        uint8_t leds = user_io_kbdled_get_status();

        if ((leds & KBD_LED_FLAG_MASK) != KBD_LED_FLAG_STATUS)
            leds = 0;

        if ((keyboard_leds & KBD_LED_CAPS_MASK) != (leds & KBD_LED_CAPS_MASK))
            hid_set_kbd_led(HID_LED_CAPS_LOCK, (leds & KBD_LED_CAPS_CONTROL) ? leds & KBD_LED_CAPS_STATUS : caps_status);

        if ((keyboard_leds & KBD_LED_NUM_MASK) != (leds & KBD_LED_NUM_MASK))
            hid_set_kbd_led(HID_LED_NUM_LOCK, (leds & KBD_LED_NUM_CONTROL) ? leds & KBD_LED_NUM_STATUS : num_status);

        if ((keyboard_leds & KBD_LED_SCRL_MASK) != (leds & KBD_LED_SCRL_MASK))
            hid_set_kbd_led(HID_LED_SCROLL_LOCK, (leds & KBD_LED_SCRL_CONTROL) ? leds & KBD_LED_SCRL_STATUS : scrl_status);

        keyboard_leds = leds;
    }
}

void user_io_check_reset(uint16_t modifiers, char useKeys)
{
    static const uint16_t combo[] = {
        0x45,  // lctrl+lalt+ralt
        0x89,  // lctrl+lgui+rgui
        0x105, // lctrl+lalt+del
    };

    if ((modifiers & ~2) == combo[useKeys])
    {
        // with lshift - MiST reset
        if (modifiers & 2)
        {
            if (mist_cfg.keep_video_mode)
                VIDEO_KEEP_VAR = VIDEO_KEEP_VALUE;
            // HW reset
            MCUReset();
        }

        if (core && core->reset)
        {
            core->reset(true);
        }
    }
    else
    {
        kbd_reset = 0;
    }
}

void user_io_hid_reset()
{
    ps2_kbd_state = PS2_KBD_IDLE;
    ps2_kbd_scan_set = 2;
    ps2_mouse_status = 0;
    ps2_mouse_state = PS2_MOUSE_IDLE;
    ps2_mouse_resolution = 0;
    ps2_mouse_samplerate = 0;
    ps2_typematic_rate = 0x80;
    autofire_joy = -1;
    autofire = 0;
}

void user_io_hid_init()
{
    InitDB9();
    user_io_hid_reset();

    // mark remap table as unused
    memset(key_remap_table, 0, sizeof(key_remap_table));
}
