#include "timer.h"

// this is a 32 bit counter which overflows after 2^32 milliseconds
// -> after 46 days

RAMFUNC void timer_delay_msec(msec_t t) {
  msec_t now = GetRTTC();
  while(GetRTTC() - now < t);
}

RAMFUNC void delay_usec(unsigned int delay)
{
  unsigned int count = (delay * (MCLK / 1000000 / 4));

  if (count > 2) count -= 2;
  else if (count == 0) return;

  asm volatile (
    "1: sub  %0, %0, #1 \n\t"
    "bne  1b"
    : "+l" (count)
    :
    : "cc"
  );
}
