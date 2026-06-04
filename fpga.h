#ifndef FPGA_H
#define FPGA_H

#include <debug.h>

unsigned char fpga_init(const char *name);
unsigned char ConfigureFpga(const char*);
unsigned char GetFPGAStatus();

static inline void BootPrint(const char *text)
{
    debugf("%s", text);
}

#endif // FPGA_H
