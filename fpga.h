#ifndef FPGA_H
#define FPGA_H

#include "timer.h"
#include "fat_compat.h"

extern char minimig_ver_beta;
extern char minimig_ver_major;
extern char minimig_ver_minor;
extern char minimig_ver_minion;

unsigned char fpga_init(const char *name);
FAST unsigned char ConfigureFpga(const char*);
FAST void SendFile(FIL *file);
FAST void SendFileEncrypted(FIL *file,unsigned char *key,int keysize);
FAST void SendFileV2(FIL* file, unsigned char* key, int keysize, int address, int size);
FAST char BootDraw(char *data, unsigned short len, unsigned short offset);
FAST char BootPrint(const char *text);
FAST char PrepareBootUpload(unsigned char base, unsigned char size);
FAST void BootExit(void);
FAST void ClearMemory(unsigned long base, unsigned long size);
unsigned char GetFPGAStatus(void);

// minimig reset stuff
#define SPI_RST_USR         0x1
#define SPI_RST_CPU         0x2
#define SPI_CPU_HLT         0x4

extern uint8_t rstval;

#endif
