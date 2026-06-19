#ifndef TOS_H
#define TOS_H

#include <user_io_core.h>
#include <hdd.h>
#include <utils.h>

// FPGA spi commands
#define MIST_WRITE_MEMORY   0x02
#define MIST_READ_MEMORY    0x03
#define MIST_SET_CONTROL    0x04
#define MIST_GET_DMASTATE   0x05 // reads state of ACSI
#define MIST_ACK_DMA        0x06 // acknowledge a DMA command
#define MIST_BUS_REQ        0x07 // request bus
#define MIST_BUS_REL        0x08 // release bus
#define MIST_SET_VADJ       0x09 // not implemented
#define MIST_NAK_DMA        0x0a // reject a DMA command

// System control bits
// 0     - RESET
// 1-3   - RAM configuration
// 4-5   - CPU configuration
// 6-7   - Floppy A+B write protection
// 8     - Color/Monochrome mode
// 10-17 - ACSI device enable
// 19    - Blitter toggle
// 20-21 - Scanlines mode
// 22    - PSG stereo toggle
// 23-24 - Chipset type
// 25    - Ethernec present
// 26-27 - USB redirection (0=Nothing, 1=RS232, 2=Printer, 3=MIDI)
// 28    - Viking enable
// 29    - Blend toggle
// 30    - Cubase enable

// RAM configurations
#define TOS_MEMCONFIG_512K        (0<<1)
#define TOS_MEMCONFIG_1M          (1<<1)
#define TOS_MEMCONFIG_2M          (2<<1)
#define TOS_MEMCONFIG_4M          (3<<1)
#define TOS_MEMCONFIG_8M          (4<<1)
#define TOS_MEMCONFIG_14M         (5<<1)

// CPU configurations
#define TOS_CPUCONFIG_68000       (0<<4)
#define TOS_CPUCONFIG_68010       (1<<4)    // not used
#define TOS_CPUCONFIG_RESERVED    (2<<4)    // not used
#define TOS_CPUCONFIG_68020       (3<<4)

// Control bits (all control bits have unknown state after core startup)
#define TOS_CONTROL_CPU_RESET     BIT(0)
#define TOS_CONTROL_FDC_WR_PROT_A BIT(6)
#define TOS_CONTROL_FDC_WR_PROT_B BIT(7)
#define TOS_CONTROL_VIDEO_COLOR   BIT(8)    // input to MFP

// Up to eight ACSI devices can be enabled
#define TOS_ACSI0_ENABLE          BIT(10)
#define TOS_ACSI1_ENABLE          BIT(11)
#define TOS_ACSI2_ENABLE          BIT(12)
#define TOS_ACSI3_ENABLE          BIT(13)
#define TOS_ACSI4_ENABLE          BIT(14)
#define TOS_ACSI5_ENABLE          BIT(15)
#define TOS_ACSI6_ENABLE          BIT(16)
#define TOS_ACSI7_ENABLE          BIT(17)

#define TOS_CONTROL_BLITTER       BIT(19)

#define TOS_CONTROL_SCANLINES0    BIT(20)   // 0 = off, 1 = 25%, 2 = 50%, 3 = 75%
#define TOS_CONTROL_SCANLINES1    BIT(21)
#define TOS_CONTROL_SCANLINES     (TOS_CONTROL_SCANLINES0 | TOS_CONTROL_SCANLINES1)

#define TOS_CONTROL_STEREO        BIT(22)
#define TOS_CONTROL_STE           BIT(23)
#define TOS_CONTROL_MSTE          BIT(24)
#define TOS_CONTROL_ETHERNET      BIT(25)
#define TOS_CONTROL_CUBASE        BIT(30)

// USB redirection modes
// NONE=0, RS232=1, PARALLEL=2, MIDI=3
#define TOS_CONTROL_REDIR0        BIT(26)
#define TOS_CONTROL_REDIR1        BIT(27)

#define TOS_CONTROL_VIKING        BIT(28)   // Viking graphics card
#define TOS_CONTROL_BLEND         BIT(29)   // Composite blending

typedef struct {
  char path[FF_LFN_BUF];
} image_path_t;

typedef struct {
  uint32_t system_ctrl;
  char cdc_control_redirect;
  char tos_img[FF_LFN_BUF];
  char cart_img[FF_LFN_BUF];
  hardfileTYPE acsi[2];
  image_path_t fdd[2];
} st_config_t;

char tos_get_cdc_control_redirect();
void tos_set_cdc_control_redirect(char mode);

unsigned long tos_system_ctrl();
void tos_update_sysctrl(unsigned long);

void assign_full_path(char *, int, const char *);

// core iface
extern const user_io_core_t mistery_core;

#endif // TOS_H
