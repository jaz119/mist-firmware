#ifndef CONFIG_H
#define CONFIG_H

#include <hdd.h>

typedef struct
{
    unsigned char lores;
    unsigned char hires;
} minimig_filter_t;

typedef struct
{
    unsigned char speed;
    unsigned char drives;
} minimig_floppy_t;

typedef struct
{
    unsigned char audiofiltermode;
    unsigned char powerledoffstate;
} minimig_features_t;

typedef struct
{
    char kickstart[FF_LFN_BUF];
    minimig_filter_t filter;
    unsigned char memory;
    unsigned char chipset;
    minimig_floppy_t floppy;
    unsigned char disable_ar3;
    unsigned char enable_ide[2];
    unsigned char scanlines;
    unsigned char pad1;
    hardfileTYPE  hardfile[HARDFILES];
    unsigned char cpu;
    unsigned char autofire;
    minimig_features_t features;
} minimig_config_t;

bool UploadKickstart(const char *);
bool LoadConfiguration(const char *, bool);

bool ConfigurationExists(const char *);
void SetConfigurationFilename(int slot);
bool SaveConfiguration(const char *);

#endif // CONFIG_H
