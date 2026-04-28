#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <ctype.h>
#include "hardware.h"
#include "utils.h"
#include "attrs.h"

unsigned int bin2bcd(unsigned int in) {
  unsigned int tens = (in * 205) >> 11;
  unsigned int units = in - (tens * 10);
  return (tens << 4) | units;
}

unsigned char bcd2bin(unsigned char in) {
  return 10*(in >> 4) + (in & 0x0f);
}

unsigned char decval(unsigned char in, unsigned char min, unsigned char max) {
  return (in == min) ? max : in-1;
}

unsigned char incval(unsigned char in, unsigned char min, unsigned char max) {
  return (in == max) ? min : in+1;
}

int _strnicmp(const char *s1, const char *s2, size_t n) {
    int v = 0;
    while (n--) {
        unsigned char c1 = *s1++;
        unsigned char c2 = *s2++;

        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;

        v = c1 - c2;
        if (v != 0 || c1 == 0) break;
    }
    return v;
}

void hexdump(const void *data, int size, int offset) {
  uint8_t i, b2c;
  uint16_t n = 0;
  const char *ptr = data;

  while(size>0) {
    iprintf("%04x: ", n + offset);

    b2c = (size>16)?16:size;
    for(i=0;i<b2c;i++)      iprintf("%02x ", 0xff&ptr[i]);
    iprintf("  ");
    for(i=0;i<(16-b2c);i++) iprintf("   ");
    for(i=0;i<b2c;i++)      iprintf("%c", isprint(ptr[i])?ptr[i]:'.');
    iprintf("\n");
    ptr  += b2c;
    size -= b2c;
    n    += b2c;
  }
}
