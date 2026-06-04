#ifndef MINIMIG_CORE_H
#define MINIMIG_CORE_H

#include <user_io_core.h>

// minimig reset stuff
#define SPI_RST_USR     0x1
#define SPI_RST_CPU     0x2
#define SPI_CPU_HLT     0x4

extern uint8_t rstval;

extern uint8_t minimig_ver_beta;
extern uint8_t minimig_ver_major;
extern uint8_t minimig_ver_minor;
extern uint8_t minimig_ver_minion;

uint16_t minimig_keycode(uint8_t key);

// core iface
extern const user_io_core_t minimig_v2_core;

#endif // MINIMIG_CORE_H
