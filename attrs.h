#ifndef ATTRS_H
#define ATTRS_H

#define RAMFUNC __attribute__ ((long_call, noinline, noclone, used, section(".ramsection"), optimize("O2")))
#define FAST __attribute__((noclone, optimize("O2")))

#ifdef CONFIG_CHIP_SAMV71
    #define FORCE_ARM __attribute__((noclone, optimize("O2")))
#else
    #define FORCE_ARM __attribute__((noclone, optimize("O2"), target("arm")))
#endif

#endif // ATTRS_H
