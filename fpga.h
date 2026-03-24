#ifndef FPGA_H
#define FPGA_H

#include "timer.h"
#include "fat_compat.h"
#include "debug.h"

extern char minimig_ver_beta;
extern char minimig_ver_major;
extern char minimig_ver_minor;
extern char minimig_ver_minion;

unsigned char fpga_init(const char *name);
unsigned char ConfigureFpga(const char*);
void SendFileV2(FIL* file, unsigned char* key, int keysize, int address, int size);
char BootDraw(char *data, unsigned short len, unsigned short offset);

static inline char BootPrint(const char *text)
{
    debugf("%s", text);
    return 0;
}

void BootExit(void);
unsigned char GetFPGAStatus(void);

// minimig reset stuff
#define SPI_RST_USR         0x1
#define SPI_RST_CPU         0x2
#define SPI_CPU_HLT         0x4

extern uint8_t rstval;

#endif
