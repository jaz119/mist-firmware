#ifndef FIRMWARE_H
#define FIRMWARE_H

#include <stdbool.h>

typedef struct
{
    unsigned long flags;
    unsigned long base;
    unsigned long size;
    unsigned long crc;
} romTYPE;

typedef struct
{
    unsigned char id[8];
    unsigned char version[16];
    romTYPE       rom;
    unsigned long padding[117];
    unsigned long crc;
} UPGRADE;

bool CheckFirmware(const char *name);
const char *GetFirmwareVersion(const char *name);

FORCE_ARM unsigned long CalculateCRC32(unsigned long crc, unsigned char *pBuffer, unsigned long nSize);
FORCE_ARM RAMFUNC void WriteFirmware(const char *name);

#endif // FIRMWARE_H
