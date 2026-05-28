#ifndef FPGA_H
#define FPGA_H

#include "timer.h"
#include "debug.h"

extern uint8_t rstval;

extern char minimig_ver_beta;
extern char minimig_ver_major;
extern char minimig_ver_minor;
extern char minimig_ver_minion;

// minimig reset stuff
#define SPI_RST_USR         0x1
#define SPI_RST_CPU         0x2
#define SPI_CPU_HLT         0x4

unsigned char fpga_init(const char *name);
unsigned char ConfigureFpga(const char*);

void SendFileV2(FIL* file, unsigned char* key, int keysize, int address, int size);

static inline void BootPrint(const char *text)
{
    debugf("%s", text);
}

unsigned char GetFPGAStatus(void);

#endif
