#ifndef ATTRS_H
#define ATTRS_H

#define RAMFUNC __attribute__ ((long_call, aligned(4), noinline, noclone, used, section(".ramsection"), optimize("O2")))
#define FAST __attribute__((noclone, optimize("O2")))

#ifdef CONFIG_CHIP_SAMV71
    #define FORCE_ARM __attribute__((noclone, optimize("O2")))
#else
    #define FORCE_ARM __attribute__((noclone, optimize("O2"), target("arm")))
    #define ALIGNED(n) __attribute__((aligned(n)))
#endif

#endif // ATTRS_H
