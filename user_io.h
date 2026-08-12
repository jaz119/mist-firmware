/*
 * user_io.h
 */

#ifndef USER_IO_H
#define USER_IO_H

#include <inttypes.h>
#include <stdbool.h>

#include <spi.h>
#include <attrs.h>
#include <user_io_core.h>
#include <cue_parser.h>
#include <hdd.h>

#define UIO_STATUS          0x00
#define UIO_BUT_SW          0x01

// codes as used by Minimig
#define UIO_JOYSTICK0       0x02  // also used by 8 bit
#define UIO_JOYSTICK1       0x03  // -"-
#define UIO_MOUSE           0x04  // -"-
#define UIO_KEYBOARD        0x05  // -"-
#define UIO_KBD_OSD         0x06  // keycodes used by OSD only

// codes as used by MiSTery
// directions (in/out) are from an io controller view
#define UIO_PARALLEL_IN     0x06
#define UIO_MIDI_OUT        0x07
#define UIO_MIDI_IN         0x08
#define UIO_ETH_MAC         0x09
#define UIO_ETH_STATUS      0x0a
#define UIO_ETH_FRM_IN      0x0b
#define UIO_ETH_FRM_OUT     0x0c
#define UIO_SERIAL_STAT     0x0d
#define UIO_KEYBOARD_IN     0x0e  // PS2 keyboard cmd
#define UIO_MOUSE_IN        0x0f  // PS2 mouse cmd

#define UIO_JOYSTICK2       0x10  // also used by minimig and 8 bit
#define UIO_JOYSTICK3       0x11  // -"-
#define UIO_JOYSTICK4       0x12  // -"-
#define UIO_JOYSTICK5       0x13  // -"-

// general codes used by 8bit, archie and MiSTery
#define UIO_GET_STRING      0x14
#define UIO_SET_STATUS      0x15
#define UIO_GET_SDSTAT      0x16  // read status of sd card emulation
#define UIO_SECTOR_RD       0x17  // SD card sector read
#define UIO_SECTOR_WR       0x18  // SD card sector write
#define UIO_SET_SDCONF      0x19  // send SD card configuration (CSD, CID)
#define UIO_ASTICK          0x1a
#define UIO_SIO_IN          0x1b  // serial in
#define UIO_SET_SDSTAT      0x1c  // set sd card status
#define UIO_SET_SDINFO      0x1d  // send info about mounted image
#define UIO_SET_STATUS2     0x1e  // 64bit status
#define UIO_GET_KBD_LED     0x1f  // keyboard LEDs control
#define UIO_SIO_OUT         0x20  // serial out
#define UIO_SET_MOD         0x21  // send core variant from metadata (ARC) file
#define UIO_SET_RTC         0x22  // send real-time-clock data
#define UIO_SD_ACK          0x23  // send ack for sector read/write
#define UIO_GET_STR_EXT     0x24  // get config string from dedicated position
#define UIO_SET_MOD2        0x25  // send core variant from metadata (ARC) file (64 bit)

// I2C bridge
#define UIO_I2C_SEND        0x30  // start i2c transaction on the FPGA side
#define UIO_I2C_GET         0x31  // get i2c status and result from the FPGA

// extended joystick control (32 bit value)
#define UIO_JOYSTICK0_EXT   0x60
#define UIO_JOYSTICK1_EXT   0x61
#define UIO_JOYSTICK2_EXT   0x62
#define UIO_JOYSTICK3_EXT   0x63
#define UIO_JOYSTICK4_EXT   0x64
#define UIO_JOYSTICK5_EXT   0x65

// extended mouse control (with wheel support)
#define UIO_MOUSE0_EXT      0x70
#define UIO_MOUSE1_EXT      0x71

#define UIO_GET_FEATS       0x80  // get core features (only once after fpga init)

#define FEAT_MENU           0x0001 // menu core
#define FEAT_PCECD          0x0002 // call pcecd_poll()
#define FEAT_QSPI           0x0004 // QSPI connection to FPGA@24MHz
#define FEAT_NEOCD          0x0008 // call neocd_poll()
#define FEAT_IDE0           0x0030 // enable primary master IDE (0 - off, 1 - ATA - 2 ATAPI CDROM)
#define FEAT_IDE0_ATA       0x0010
#define FEAT_IDE0_CDROM     0x0020
#define FEAT_IDE1           0x00c0 // enable primary slave IDE
#define FEAT_IDE1_ATA       0x0040
#define FEAT_IDE1_CDROM     0x0080
#define FEAT_IDE2           0x0300 // enable secondary master IDE
#define FEAT_IDE2_ATA       0x0100
#define FEAT_IDE2_CDROM     0x0200
#define FEAT_IDE3           0x0c00 // enable secondary slave IDE
#define FEAT_IDE3_ATA       0x0400
#define FEAT_IDE3_CDROM     0x0800
#define FEAT_IDE_MASK       0x0ff0
#define FEAT_PS2REP         0x1000 // typematic repeat by default
#define FEAT_BIGOSD         0x2000 // 16 line tall OSD
#define FEAT_HDMI           0x4000 // HDMI output
#define FEAT_PSX            0x8000 // PSX-specific CD image handling

#define CONF_SCANDBL_DIS    BIT(4)
#define CONF_YPBPR          BIT(5)
#define CONF_CSYNC_DISABLE  BIT(6)
#define CONF_SDRAM64        BIT(7)

// core type value should be unlikely to be returned by broken cores
#define CORE_TYPE_UNKNOWN   0x55
#define CORE_TYPE_DUMB      0xa0   // core without any io controller interaction
#define CORE_TYPE_MINIMIG   0xa1   // legacy Minimig Amiga core
#define CORE_TYPE_PACE      0xa2   // core from pacedev.net (joystick only)
#define CORE_TYPE_MIST      0xa3   // legacy Atari ST core
#define CORE_TYPE_8BIT      0xa4   // generic core type
#define CORE_TYPE_MINIMIG_V2 0xa5  // Minimig Amiga with AGA
#define CORE_TYPE_ARCHIE    0xa6   // Acorn Archimedes core
#define CORE_TYPE_MISTERY   0xa7   // MiSTery, modern Atari ST core

// user io status bits (currently only used by 8bit)
#define UIO_STATUS_RESET    0x01

#define UIO_STOP_BIT_1      0
#define UIO_STOP_BIT_1_5    1
#define UIO_STOP_BIT_2      2

#define UIO_PARITY_NONE     0
#define UIO_PARITY_ODD      1
#define UIO_PARITY_EVEN     2
#define UIO_PARITY_MARK     3
#define UIO_PARITY_SPACE    4

#define BUTTON_MENU         BIT(0)
#define BUTTON_USER         BIT(1)
#define SWITCH_DEBUG        BIT(2)
#define SWITCH_CORE         BIT(3)

// NIC Status Register layout (NE2000)
#define NIC_STAT_CODE(s)    (((s) >> 24) & 0xffUL)  // Core Status code
#define NIC_STAT_TX_RDY     BIT(18)                 // TX DMA is ready
#define NIC_STAT_ISR_PTX    BIT(17)                 // Packet Transmitted with no error
#define NIC_STAT_ISR_PRX    BIT(16)                 // Packet Received with no error
#define NIC_STAT_TBCR(s)    ((s) & 0x0000ffffUL)    // Transmitter Byte Count Register

// NIC Status codes
#define NIC_STATUS_IDLE     0xfe
#define NIC_STATUS_TX_PENDING  0xa5
#define NIC_STATUS_TX_DONE  0x12

extern uint32_t core_type; // current core type
extern const user_io_core_t *core; // current core support module
extern bool osd_is_visible;

// serial status data type returned from the core
typedef struct {
    uint32_t bitrate;       // 300, 600 ... 115200
    uint8_t datasize;       // 5,6,7,8 ...
    uint8_t parity;
    uint8_t stopbits;
    uint8_t fifo_stat;      // space in cores input fifo
} __attribute__ ((packed)) serial_status_t;

void user_io_reset();
void user_io_init();
void user_io_detect_core_type();
void user_io_init_core();
uint32_t user_io_get_core_features();

static inline uint32_t user_io_core_type() {
    return core_type;
}

static inline bool minimig_v2() {
    return (user_io_core_type() == CORE_TYPE_MINIMIG_V2);
}

void user_io_poll();
void user_io_osd_key_enable(bool);
void user_io_serial_tx(char *, uint16_t);
bool user_io_serial_status(serial_status_t *, uint8_t);
bool user_io_is_mounted(int index);
bool user_io_file_mount(const unsigned char *, int);
char user_io_cue_mount(const unsigned char *, int);
void user_io_sd_ack(uint8_t drive_index);
void user_io_sd_set_config();

// io controllers interface for FPGA Ethernet emulation
// using usb devices attached to the io controller
uint32_t user_io_eth_get_status();
void user_io_eth_send_mac(const uint8_t *);
void user_io_eth_send_rx_frame(uint8_t *, uint16_t);
void user_io_eth_receive_tx_frame(uint8_t *, uint16_t);

#define CONFIG_ROOT 1   // create config filename in the root directory
#define CONFIG_VHD  2   // create config filename according to VHD= in arc file

static inline bool user_io_osd_is_visible() {
    return osd_is_visible;
}

static inline bool user_io_is_cue_mounted() {
    return toc.valid;
}

void user_io_change_into_core_dir();

#ifdef HAVE_HDMI
char user_io_i2c_write(uint8_t addr, uint8_t subaddr, uint8_t data);
char user_io_i2c_read(uint8_t addr, uint8_t subaddr, uint8_t *data);
bool user_io_hdmi_detected();
#endif // HAVE_HDMI

#endif // USER_IO_H
