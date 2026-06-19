#ifndef ARCHIE_H
#define ARCHIE_H

#include <user_io_core.h>
#include <hdd.h>

typedef struct {
  unsigned long system_ctrl;     // system control word
  char rom_img[64];              // rom image file name
  char cmos_img[64];             // cmos image file name
  hardfileTYPE hardfile[2];
} archie_config_t;

// core iface
extern const user_io_core_t archie_core;

#endif // ARCHIE_H
