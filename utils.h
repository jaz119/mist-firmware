#ifndef _UTILS_H_
#define _UTILS_H_

#include <stddef.h>
#include <stdint.h>
#include "attrs.h"

#define MIN(a, b)       (((a) < (b)) ? (a) : (b))

#define PRIu64f         "%lx%08lx"
#define PRIu64_LOW(x)   (uint32_t)((x) >> 32)
#define PRIu64_HIGH(x)  (uint32_t)((x) & 0xFFFFFFFFu)
#define PRIu64_PAIR(x)  PRIu64_LOW(x), PRIu64_HIGH(x)

#ifndef CONFIG_CHIP_SAMV71
    #define ARRAY_SIZE(a)   (sizeof(a) / sizeof(a[0]))
#endif

unsigned char decval(unsigned char in, unsigned char min, unsigned char max);
unsigned char incval(unsigned char in, unsigned char min, unsigned char max);
FAST unsigned char bin2bcd(unsigned char in);
FAST unsigned char bcd2bin(unsigned char in);
FAST int _strnicmp(const char *s1, const char *s2, size_t n);
void hexdump(void *data, uint16_t size, uint16_t offset);

#endif
