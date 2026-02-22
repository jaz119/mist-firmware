#ifndef IKBD_H
#define IKBD_H

#include "attrs.h"

void ikbd_init(void);
void ikbd_poll(void);
void ikbd_reset(void);
void ikbd_joystick(unsigned char joy, uint32_t map);
void ikbd_mouse(unsigned char buttons, int x, int y);
void ikbd_keyboard(unsigned char code);
FAST void ikbd_update_time(void);

#endif // IKBD_H
