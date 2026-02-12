// interface between USB timer and minimig timer

#ifndef TIMER_H
#define TIMER_H

#include <inttypes.h>
#include <stdbool.h>

#include "hardware.h"
#include "attrs.h"

typedef uint32_t msec_t;

void timer_init();

static inline msec_t timer_get_msec() {
  return GetRTTC();
}

FAST void timer_delay_msec(msec_t t);
FAST bool timer_check(msec_t ref, msec_t delay);
RAMFUNC void delay_usec(unsigned int);

#endif // TIMER_H
