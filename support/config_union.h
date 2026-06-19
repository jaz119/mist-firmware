#ifndef CONFIG_UNION_H
#define CONFIG_UNION_H

#include <8bit/core.h>
#include <neocd.h>
#include <pcecd.h>
#include <minimig/fdd.h>
#include <minimig/config.h>
#include <archie.h>
#include <hdd.h>
#include <tos.h>

// Put core runtime data into this union for lower memory usage
typedef union {
    struct {
        archie_config_t archie;
        char floppy_name[2][64];
    }; // ARCHIE
    struct {
        adfTYPE df[4];
        hdfTYPE hdf[HARDFILES];
        minimig_config_t minimig;
        minimig_config_t minimig_tmp;
        char filename[16];
    }; // MINIMIG
    struct {
        st_config_t st;
        char fname[16];
    }; // MISTery
    struct {
        union {
            neocd_t neocdd;
            pcecd_t pcecdd;
        };
        hardfileTYPE hardfiles[HARDFILES];
        uint16_t conf_idx[CONF_TBL_MAX];
        char buffer[128 + 1];
    }; // 8BIT
} united_config_t;

extern united_config_t config;

#endif // CONFIG_UNION_H
