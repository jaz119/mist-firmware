#ifndef CONFIG_H
#define CONFIG_H

#include "hdd.h"

typedef struct
{
    unsigned char lores;
    unsigned char hires;
} filterTYPE;

typedef struct
{
    unsigned char speed;
    unsigned char drives;
} floppyTYPE;

typedef struct
{
    unsigned char audiofiltermode;
    unsigned char powerledoffstate;
} featuresTYPE;

typedef struct
{
    char          kickstart[80];
    filterTYPE    filter;
    unsigned char memory;
    unsigned char chipset;
    floppyTYPE    floppy;
    unsigned char disable_ar3;
    unsigned char enable_ide[2];
    unsigned char scanlines;
    unsigned char pad1;
    hardfileTYPE  hardfile[HARDFILES];
    unsigned char cpu;
    unsigned char autofire;
    featuresTYPE  features;
} configTYPE;

extern configTYPE config;

bool UploadKickstart(const char *);
bool LoadConfiguration(const char *, bool);

bool ConfigurationExists(const char *);
void SetConfigurationFilename(int slot);
bool SaveConfiguration(const char *);

#endif // CONFIG_H
