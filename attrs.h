#ifndef ATTRS_H
#define ATTRS_H

#ifndef ALIGNED
    #define ALIGNED(n)  __attribute__((aligned(n)))
#endif

#ifdef CONFIG_CHIP_SAMV71
    #define FAST
    #define FORCE_ARM
    #define RAMFUNC     __attribute__((aligned(32), section(".ramsection"), noclone, noinline, long_call, used))
#else
    #define FAST        __attribute__((aligned(4), optimize("O2"), noclone))
    #define FORCE_ARM   __attribute__((aligned(4), optimize("O2"), target("arm"), noclone))
    #define RAMFUNC     __attribute__((aligned(4), optimize("O2"), section(".ramsection"), noclone, noinline, long_call, used))
#endif

#endif // ATTRS_H
