#ifndef _UTILS_H_
#define _UTILS_H_

#include <stddef.h>
#include <stdint.h>
#include "attrs.h"

#define BIT(nr)         (1UL << (nr))
#define MIN(a, b)       (((a) < (b)) ? (a) : (b))
#define MAX(a, b)       (((a) > (b)) ? (a) : (b))

#ifndef ARRAY_SIZE
    #define ARRAY_SIZE(a)   (sizeof(a) / sizeof(a[0]))
#endif

static inline int32_t ABS(int32_t value) {
    int32_t mask = value >> 31;
    return (value ^ mask) - mask;
}

// for tiny version of newlib
#define PRIu64_llx      "%lx%08lx"
#define PRIu64_LOW(x)   (uint32_t)((x) >> 32)
#define PRIu64_HIGH(x)  (uint32_t)((x) & 0xFFFFFFFFu)

unsigned char decval(unsigned char in, unsigned char min, unsigned char max);
unsigned char incval(unsigned char in, unsigned char min, unsigned char max);

unsigned int bin2bcd(unsigned int in);
unsigned char bcd2bin(unsigned char in);

int _strnicmp(const char *s1, const char *s2, size_t n);
void hexdump(const void *data, int size, int offset);

#endif
