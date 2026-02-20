#include "AT91SAM7S256.h"

#ifndef HARDWARE_H
#define HARDWARE_H

#include <inttypes.h>

#include "usb.h"
#include "usb/rtc.h"
#include "attrs.h"

#define MCLK 48000000
#define FWS 1 // Flash wait states

#define IFLASH_ADDR     AT91C_IFLASH
#define IFLASH_SIZE     AT91C_IFLASH_SIZE
#define FLASH_PAGESIZE  AT91C_IFLASH_PAGE_SIZE

#define DISKLED         AT91C_PIO_PA29
#define DISKLED_ON      ( *AT91C_PIOA_CODR = DISKLED )
#define DISKLED_OFF     ( *AT91C_PIOA_SODR = DISKLED )
#define DISKLED_TOGGLE  { if (*AT91C_PIOA_ODSR & DISKLED) { DISKLED_ON; } else { DISKLED_OFF; } }

#define MMC_SEL         AT91C_PIO_PA31

#define USB_SEL         AT91C_PIO_PA11
#define USB_INT         AT91C_PIO_PA30
#define USB_PUP         AT91C_PIO_PA16

#define SD_WP           AT91C_PIO_PA1
#define SD_CD           AT91C_PIO_PA0

// fpga programming interface
#define FPGA_OER        *AT91C_PIOA_OER
#define FPGA_SODR       *AT91C_PIOA_SODR
#define FPGA_CODR       *AT91C_PIOA_CODR
#define FPGA_PDSR       *AT91C_PIOA_PDSR
#define FPGA_DONE_PDSR  FPGA_PDSR
#define FPGA_DATA0_CODR FPGA_CODR
#define FPGA_DATA0_SODR FPGA_SODR

#ifdef EMIST

// Xilinx programming interface
#define XILINX_DONE     AT91C_PIO_PA4
#define XILINX_DIN      AT91C_PIO_PA9
#define XILINX_INIT_B   AT91C_PIO_PA8
#define XILINX_PROG_B   AT91C_PIO_PA7
#define XILINX_CCLK     AT91C_PIO_PA15
#else

// Altera programming interface
#define ALTERA_DONE     AT91C_PIO_PA4
#define ALTERA_DATA0    AT91C_PIO_PA9
#define ALTERA_NCONFIG  AT91C_PIO_PA8
#define ALTERA_NSTATUS  AT91C_PIO_PA7
#define ALTERA_DCLK     AT91C_PIO_PA15

#define ALTERA_START_CONFIG
#define ALTERA_STOP_CONFIG
#define ALTERA_NCONFIG_SET   FPGA_SODR = ALTERA_NCONFIG
#define ALTERA_NCONFIG_RESET FPGA_CODR = ALTERA_NCONFIG
#define ALTERA_DCLK_SET      FPGA_SODR = ALTERA_DCLK
#define ALTERA_DCLK_RESET    FPGA_CODR = ALTERA_DCLK
#define ALTERA_DATA0_SET     FPGA_DATA0_SODR = ALTERA_DATA0;
#define ALTERA_DATA0_RESET   FPGA_DATA0_CODR = ALTERA_DATA0;

#define ALTERA_NSTATUS_STATE (FPGA_PDSR & ALTERA_NSTATUS)
#define ALTERA_DONE_STATE    (FPGA_DONE_PDSR & ALTERA_DONE)

#endif // EMIST

// db9 joystick ports
#define JOY1_UP         AT91C_PIO_PA28
#define JOY1_DOWN       AT91C_PIO_PA27
#define JOY1_LEFT       AT91C_PIO_PA26
#define JOY1_RIGHT      AT91C_PIO_PA25
#define JOY1_BTN1       AT91C_PIO_PA24
#define JOY1_BTN2       AT91C_PIO_PA23
#define JOY1  (JOY1_UP|JOY1_DOWN|JOY1_LEFT|JOY1_RIGHT|JOY1_BTN1|JOY1_BTN2)

#define JOY0_UP         AT91C_PIO_PA22
#define JOY0_DOWN       AT91C_PIO_PA21
#define JOY0_LEFT       AT91C_PIO_PA20
#define JOY0_RIGHT      AT91C_PIO_PA19
#define JOY0_BTN1       AT91C_PIO_PA18
#define JOY0_BTN2       AT91C_PIO_PA17
#define JOY0  (JOY0_UP|JOY0_DOWN|JOY0_LEFT|JOY0_RIGHT|JOY0_BTN1|JOY0_BTN2)

// chip selects for FPGA communication
#define FPGA0 AT91C_PIO_PA10
#define FPGA1 AT91C_PIO_PA3
#define FPGA2 AT91C_PIO_PA2

#define FPGA3           AT91C_PIO_PA9   // same as ALTERA_DATA0

#define VBL             AT91C_PIO_PA7

#define USB_LOAD_VAR         *(int*)(0x0020FF04)
#define USB_LOAD_VALUE       12345678

#define DEBUG_MODE_VAR       *(int*)(0x0020FF08)
#define DEBUG_MODE_VALUE     87654321
#define DEBUG_MODE           (DEBUG_MODE_VAR == DEBUG_MODE_VALUE)

#define VIDEO_KEEP_VALUE     0x87654321
#define VIDEO_KEEP_VAR       (*(int*)0x0020FF10)
#define VIDEO_ALTERED_VAR    (*(uint8_t*)0x0020FF14)
#define VIDEO_SD_DISABLE_VAR (*(uint8_t*)0x0020FF15)
#define VIDEO_YPBPR_VAR      (*(uint8_t*)0x0020FF16)

#define USB_BOOT_VALUE       0x8007F007
#define USB_BOOT_VAR         (*(int*)0x0020FF18)

#define SECTOR_BUFFER_SIZE   4096

// MAX3421E INT pin polling
static inline uint8_t usb_irq_active() {
  return !(AT91C_BASE_PIOA->PIO_PDSR & USB_INT);
}

static inline char mmc_inserted() {
  return !(*AT91C_PIOA_PDSR & SD_CD);
}

static inline char mmc_write_protected() {
  return !!(*AT91C_PIOA_PDSR & SD_WP);
}

void USART_Init(unsigned long baudrate);
void USART_Write(unsigned char c);
unsigned char USART_Read(void);
void USART_Poll(void);

void Timer_Init(void);

// 12 bits accuracy at 1ms = 4096 ms
static inline unsigned long GetTimer(unsigned long offset)
{
    unsigned long systimer = (*AT91C_PITC_PIIR & AT91C_PITC_PICNT);
    systimer += offset << 20;
    return (systimer); // valid bits [31:20]
}

static inline unsigned long CheckTimer(unsigned long time)
{
    unsigned long systimer = (*AT91C_PITC_PIIR & AT91C_PITC_PICNT);
    time -= systimer;
    return(time > (1UL << 31));
}

RAMFUNC void WaitTimer(unsigned long time);

static inline void MCUReset() {
  *AT91C_RSTC_RCR = 0xA5 << 24 | AT91C_RSTC_PERRST | AT91C_RSTC_PROCRST | AT91C_RSTC_EXTRST;
}

void InitRTTC();

static inline unsigned long GetRTTC() {
  return (AT91C_BASE_RTTC->RTTC_RTVR);
}

int GetSPICLK();

extern volatile unsigned char adc_state;

void InitADC(void);
RAMFUNC void PollADC();

// user, menu, DIP2, DIP1
static inline unsigned char UserButton() {
  return !!(adc_state & 8);
}

static inline unsigned char MenuButton() {
  return !!(adc_state & 4);
}

static inline bool is_dip_switch2_on() {
  return !!(adc_state & 1);
}

static inline bool is_dip_switch1_on() {
  return !!(adc_state & 2) || DEBUG_MODE;
}

static inline bool CheckButton() {
#ifdef BUTTON
    return((~*AT91C_PIOA_PDSR) & BUTTON);
#else
    return MenuButton();
#endif
}

static inline void InitDB9() {};
FAST char GetDB9(char index, uint16_t *joy_map);

static inline char GetRTC(unsigned char *d) {
  return usb_rtc_get_time(d);
}

static inline char SetRTC(unsigned char *d) {
  return usb_rtc_set_time(d);
}

RAMFUNC void UnlockFlash();
RAMFUNC void WriteFlash(int page);

#define DEBUG_FUNC_IN()
#define DEBUG_FUNC_OUT()

#ifdef __GNUC__
void __init_hardware(void);
#endif

#endif // HARDWARE_H
