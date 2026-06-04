#include <user_io.h>
#include <user_io_hid.h>
#include <minimig/core.h>
#include <8bit/core.h>
#include <usb/timer.h>
#include <keycodes.h>
#include <spi.h>
#include <fpga.h>
#include <boot.h>
#include <config.h>
#include <fdd.h>
#include <arc_file.h>
#include <mist_cfg.h>

#define BREAK 0x8000

uint8_t rstval = 0;

uint8_t minimig_ver_beta;
uint8_t minimig_ver_major;
uint8_t minimig_ver_minor;
uint8_t minimig_ver_minion;

// 16 bytes fifo for amiga key codes
// to limit max key rate sent into the core
#define KBD_FIFO_SIZE 16 // must be power of 2
static uint16_t kbd_fifo[KBD_FIFO_SIZE];
static uint8_t kbd_fifo_r = 0, kbd_fifo_w = 0;
static uint32_t kbd_timer = 0;

#define X   0
#define Y   1
#define Z   2

static void kbd_fifo_send(uint16_t code)
{
    spi_uio_cmd8((code & OSD) ? UIO_KBD_OSD : UIO_KEYBOARD, code & 0xff);

    // next key after 10ms earliest
    kbd_timer = GetTimer(10);
}

static void kbd_fifo_enqueue(uint16_t code)
{
    // if fifo full just drop the value. This should never happen
    if (((kbd_fifo_w + 1) & (KBD_FIFO_SIZE - 1)) == kbd_fifo_r)
        return;

    // store in queue
    kbd_fifo[kbd_fifo_w] = code;
    kbd_fifo_w = (kbd_fifo_w + 1) & (KBD_FIFO_SIZE - 1);
}

// send pending bytes if timer has run up
static void kbd_fifo_poll()
{
    // timer enabled and running?
    if (kbd_timer && !CheckTimer(kbd_timer))
        return;

    // timer == 0 means timer is not running anymore
    kbd_timer = 0;

    if (kbd_fifo_w == kbd_fifo_r)
        return;

    kbd_fifo_send(kbd_fifo[kbd_fifo_r]);
    kbd_fifo_r = (kbd_fifo_r + 1) & (KBD_FIFO_SIZE - 1);
}

static void io_kbd_minimig(uint16_t code)
{
    // amiga has "break" marker in msb
    if (code & BREAK)
        code = (code & 0xff) | 0x80;

    // send immediately if possible
    if (CheckTimer(kbd_timer) && (kbd_fifo_w == kbd_fifo_r)) {
        kbd_fifo_send(code);
    } else {
        kbd_fifo_enqueue(code);
    }
}

uint16_t minimig_keycode(uint8_t key)
{
    // replace MENU key by RGUI to allow using
    // Right Amiga on reduced keyboards (it also disables the use of Menu for OSD)
    if (mist_cfg.key_menu_as_rgui && mist_cfg.keyrah_mode == 0 && key == 0x65)
    {
        return 0x67;
    }

    return usb2ami[key];
}

static uint16_t minimig_modify_keycode(uint8_t key)
{
    /* usb modifer bits:
        0     1      2    3    4     5      6    7
        LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
    */
    static const uint16_t amiga_modifier[] = {
        0x63, 0x60, 0x64, 0x66, 0x63, 0x61, 0x65, 0x67
    };

    return amiga_modifier[key];
}

static void io_mouse_minimig(uint8_t idx, uint8_t b, char x, char y, char z)
{
    mouse_pos[idx][X] += x;
    mouse_pos[idx][Y] += y;
    mouse_pos[idx][Z] += z;
    mouse_flags[idx] |= 0x80 | (b & 7);
}

static void mouse_poll()
{
    // frequently check mouse for events
    if (!CheckTimer(mouse_timer))
        return;

    mouse_timer = GetTimer(MOUSE_FREQ);

    for (char idx = 0; idx < 2; idx++)
    {
        if (!(mouse_flags[idx] & 0x80))
            continue;

        int x, y, z;

        // ----- X axis -------
        if (mouse_pos[idx][X] < -128) {
            x = -128;
            mouse_pos[idx][X] += 128;
        } else if (mouse_pos[idx][X] > 127) {
            x = 127;
            mouse_pos[idx][X] -= 127;
        } else {
            x = mouse_pos[idx][X];
            mouse_pos[idx][X] = 0;
        }

        // ----- Y axis -------
        if (mouse_pos[idx][Y] < -128) {
            y = (-128);
            mouse_pos[idx][Y] += 128;
        } else if (mouse_pos[idx][Y] > 127) {
            y = 127;
            mouse_pos[idx][Y] -= 127;
        } else {
            y = mouse_pos[idx][Y];
            mouse_pos[idx][Y] = 0;
        }

        // ----- Z axis -------
        if (mouse_pos[idx][Z] < -128) {
            z = (-128);
            mouse_pos[idx][Z] += 128;
        } else if (mouse_pos[idx][Z] > 127) {
            z = 127;
            mouse_pos[idx][Z] -= 127;
        } else {
            z = mouse_pos[idx][Z];
            mouse_pos[idx][Z] = 0;
        }

        if (!idx) {
            // send the first mouse only with the old message
            spi_uio_cmd_cont(UIO_MOUSE);
            spi8(x);
            spi8(y);
            spi8(mouse_flags[idx] & 0x07);
            DisableIO();
        }

        spi_uio_cmd_cont(UIO_MOUSE0_EXT + idx);
        spi8(x);
        spi8(y);
        spi8(mouse_flags[idx] & 0x07);
        spi8(z);
        DisableIO();

        // reset flags
        mouse_flags[idx] = 0;
    }
}

static void minimig_eject_all()
{
  for (int i = 0; i < ARRAY_SIZE(df); i++)
  {
    df[i].status = 0;
  }

  config.hardfile[0].present = 0;
  config.hardfile[1].present = 0;
}

static void minimig_init()
{
    user_io_8bit_set_status(minimig_cfg.clock_freq << 1, 0xffffffff);
    WaitTimer(100); // delay for PLL

    EnableOsd();
    SPI(OSD_CMD_VERSION);
    minimig_ver_beta   = SPI(0xff);
    minimig_ver_major  = SPI(0xff);
    minimig_ver_minor  = SPI(0xff);
    minimig_ver_minion = SPI(0xff);
    DisableOsd();

    delay_usec(1);

    EnableOsd();
    SPI(OSD_CMD_RST);
    rstval = (SPI_RST_USR | SPI_RST_CPU | SPI_CPU_HLT); // reset #1
    SPI(rstval);
    DisableOsd();

    delay_usec(50);

    EnableOsd();
    SPI(OSD_CMD_RST);
    rstval = (SPI_RST_CPU | SPI_CPU_HLT); // reset #2
    SPI(rstval);
    DisableOsd();

    WaitTimer(100); // video sync delay
    BootInit();

    WaitTimer(250);
    char rtl_ver[45];
    siprintf(rtl_ver, "*** MINIMIG-AGA%s v%d.%d.%d for MiST ***",
        minimig_ver_beta ? " BETA" : "",
        minimig_ver_major, minimig_ver_minor, minimig_ver_minion);
    BootPrintEx(rtl_ver);
    BootPrintEx(" ");
    BootPrintEx("MINIMIG-AGA for MiST by Rok Krajnc (rok.krajnc@gmail.com)");
    BootPrintEx("Original Minimig by Dennis van Weeren");
    BootPrintEx("Updates by Jakub Bednarski, Tobias Gubener, Sascha Boing, A.M. Robinson & others");
    BootPrintEx("MiST by Till Harbaum (till@harbaum.org)");
    BootPrintEx(" ");
    BootPrintEx(" ");

    minimig_eject_all();

    config.kickstart[0] = 0;
    SetConfigurationFilename(arc_get_cfg_file_n());

    // use slot-based config filename
    LoadConfiguration(NULL, true);
}

static void minimig_reset(bool)
{
    OsdReset(RESET_NORMAL);
}

static void handle_drives()
{
    EnableFpga();
    uint8_t c1 = SPI(0); // cmd request and drive number
    uint8_t c2 = SPI(0); // track number
    SPI(0);
    SPI(0);
    SPI(0);
    SPI(0);
    DisableFpga();

    HandleFDD(c1, c2);
    HandleHDD(c1, c2, 1);

    UpdateFDDStatus();
}

static void minimig_poll()
{
    kbd_fifo_poll();
    mouse_poll();
    handle_drives();
}

// core iface
const user_io_core_t minimig_v2_core = {
    .init = minimig_init,
    .poll = minimig_poll,
    .reset = minimig_reset,
    .keycode = minimig_keycode,
    .modify_keycode = minimig_modify_keycode,
    .send_keycode = io_kbd_minimig,
    .send_mouse = io_mouse_minimig,
    .send_analog_joy = send_analog_joystick,
    .send_digital_joy = send_digital_joystick,
    .eject_all = minimig_eject_all,
    .name = "Minimig",
};
