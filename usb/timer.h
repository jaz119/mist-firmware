// interface between USB timer and minimig timer

#ifndef TIMER_H
#define TIMER_H

#include <inttypes.h>
#include <stdbool.h>

#include "hardware.h"

typedef uint32_t msec_t;

static inline void timer_init() {
  // reprogram the realtime timer to run at 1Khz
  InitRTTC();
}

static inline msec_t timer_get_msec() {
  return GetRTTC();
}

static inline bool timer_check(msec_t ref, msec_t delay) {
  msec_t now = GetRTTC();
  return ((now-ref) >= delay);
}

RAMFUNC void timer_delay_msec(msec_t t);
RAMFUNC void delay_usec(unsigned int);

#endif // TIMER_H
