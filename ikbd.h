#ifndef IKBD_H
#define IKBD_H

#include "attrs.h"

#ifdef LEGACY_ST_IKBD // for old MiST core

void ikbd_init(void);
void ikbd_poll(void);
void ikbd_reset(void);
void ikbd_joystick(unsigned char joy, uint32_t map);
void ikbd_mouse(unsigned char buttons, int x, int y);
void ikbd_keyboard(unsigned char code);
FAST void ikbd_update_time(void);

#else // only new MiSTery based core is supported

static inline void ikbd_init(void) {};
static inline void ikbd_poll(void) {};
static inline void ikbd_reset(void) {};
static inline void ikbd_joystick(unsigned char, uint32_t) {};
static inline void ikbd_mouse(unsigned char, int, int) {};
static inline void ikbd_keyboard(unsigned char) {};
static inline void ikbd_update_time(void) {};

#endif

#endif // IKBD_H
