#ifndef SPI_H
#define SPI_H

#include "AT91SAM7S256.h"
#include <inttypes.h>

#include "hardware.h"
#include "attrs.h"

/* main init functions */
void spi_init(void);
void spi_slow();
void spi_fast();
void spi_fast_mmc();
unsigned char spi_get_speed();
void spi_set_speed(unsigned char speed);

static inline void spi_wait4xfer_end() {
  while (!(*AT91C_SPI_SR & AT91C_SPI_TXEMPTY));

  /* Clear any data left in the receiver */
  (void)*AT91C_SPI_RDR;
  (void)*AT91C_SPI_RDR;
}

/* chip select functions */
#define EnableFpgaMinimig EnableFpga

void EnableOsd(void);
void DisableOsd(void);

static inline void EnableCard() {
    *AT91C_SPI_MR = AT91C_SPI_MSTR | AT91C_SPI_MODFDIS  | (0x01 << 16); // NPCS1
    AT91C_BASE_PIOA->PIO_PDR = MMC_SEL;
    *AT91C_SPI_CR = AT91C_SPI_SPIEN;
}

static inline void DisableCard() {
    *AT91C_SPI_CR = AT91C_SPI_SPIEN | AT91C_SPI_LASTXFER;
    spi_wait4xfer_end();
    AT91C_BASE_PIOA->PIO_PER = MMC_SEL;
}

static inline void EnableFpga()
{
    *AT91C_SPI_CR = AT91C_SPI_SPIEN;
    *AT91C_SPI_MR = AT91C_SPI_MSTR | AT91C_SPI_MODFDIS  | (0x03 << 16); // NPCS2
}

static inline void DisableFpga()
{
    *AT91C_SPI_CR = AT91C_SPI_SPIEN | AT91C_SPI_LASTXFER;
    spi_wait4xfer_end();
    *AT91C_SPI_MR = AT91C_SPI_MSTR | AT91C_SPI_MODFDIS  | (0x01 << 16); // NPCS1
}

#ifdef FPGA3
// the MiST has the user inout on the arm controller
static inline void EnableIO() {
    *AT91C_SPI_MR = AT91C_SPI_MSTR | AT91C_SPI_MODFDIS  | (0x01 << 16); // NPCS1
    *AT91C_SPI_CR = AT91C_SPI_SPIEN;
    AT91C_BASE_PIOA->PIO_PDR = FPGA3;
}

static inline void DisableIO() {
    spi_wait4xfer_end();
    AT91C_BASE_PIOA->PIO_PER = FPGA3;
}
#endif

static inline void EnableDMode() {
  *AT91C_PIOA_CODR = FPGA2;    // enable FPGA2 output
}

static inline void DisableDMode() {
  *AT91C_PIOA_SODR = FPGA2;    // disable FPGA2 output
}

RAMFUNC unsigned char SPI(unsigned char);

/* generic helper */
static inline unsigned char spi_in() {
  return SPI(0);
}

static inline void spi8(unsigned char parm) {
  SPI(parm);
}

static inline void spi16(unsigned short parm) {
  SPI(parm >> 8);
  SPI(parm >> 0);
}

static inline void spi16le(unsigned short parm) {
  SPI(parm >> 0);
  SPI(parm >> 8);
}

static inline void spi24(unsigned long parm) {
  SPI(parm >> 16);
  SPI(parm >> 8);
  SPI(parm >> 0);
}

static inline void spi32(unsigned long parm) {
  SPI(parm >> 24);
  SPI(parm >> 16);
  SPI(parm >> 8);
  SPI(parm >> 0);
}

static inline void spi32le(unsigned long parm) {
  SPI(parm >> 0);
  SPI(parm >> 8);
  SPI(parm >> 16);
  SPI(parm >> 24);
}

static inline void spi_n(unsigned char value, unsigned short cnt) {
  while(cnt--)
    SPI(value);
}

/* block transfer functions */
RAMFUNC void spi_read(char *addr, uint16_t len);

static inline void spi_block_read(char *addr) {
  spi_read(addr, 512);
}

RAMFUNC void spi_write(const char *addr, uint16_t len);

static inline void spi_block_write(const char *addr) {
  spi_write(addr, 512);
}

RAMFUNC void spi_block(unsigned short num);

/* OSD related SPI functions */
void spi_osd_cmd_cont(unsigned char cmd);
void spi_osd_cmd(unsigned char cmd);
void spi_osd_cmd8_cont(unsigned char cmd, unsigned char parm);
void spi_osd_cmd8(unsigned char cmd, unsigned char parm);
void spi_osd_cmd32_cont(unsigned char cmd, unsigned long parm);
void spi_osd_cmd32(unsigned char cmd, unsigned long parm);
void spi_osd_cmd32le_cont(unsigned char cmd, unsigned long parm);
void spi_osd_cmd32le(unsigned char cmd, unsigned long parm);

/* User_io related SPI functions */
void spi_uio_cmd_cont(unsigned char cmd);
void spi_uio_cmd(unsigned char cmd);
void spi_uio_cmd8(unsigned char cmd, unsigned char parm);
void spi_uio_cmd8_cont(unsigned char cmd, unsigned char parm);
void spi_uio_cmd32(unsigned char cmd, unsigned long parm);
void spi_uio_cmd64(unsigned char cmd, unsigned long long parm);

/* spi functions for max3421 */
static inline void spi_max_start() {
    *AT91C_SPI_CR = AT91C_SPI_SPIEN;
    *AT91C_SPI_MR = AT91C_SPI_MSTR | AT91C_SPI_MODFDIS  | (0x0E << 16); // NPCS0
}

static inline void spi_max_end() {
    *AT91C_SPI_CR = AT91C_SPI_SPIEN | AT91C_SPI_LASTXFER;
    spi_wait4xfer_end();
    *AT91C_SPI_MR = AT91C_SPI_MSTR | AT91C_SPI_MODFDIS  | (0x01 << 16); // NPCS1
}

#define SPI_SDC_CLK_VALUE 2     // 24 MHz
#define SPI_MMC_CLK_VALUE 3     // 16 MHz
#define SPI_SLOW_CLK_VALUE 120  // 400kHz

// for old minimig core
#define SPI_MINIMIGV1_HACK SPI(0xff);

#endif // SPI_H
