#ifndef ATTRS_H
#define ATTRS_H

#ifndef ALIGNED
    #define ALIGNED(n)  __attribute__((aligned(n)))
#endif

#ifdef CONFIG_ARCH_ARMV7M
    #define FAST
    #define FORCE_ARM
    #define RAMFUNC     __attribute__((optimize("O2"), section(".ramsection"), long_call, noinline, noclone))
#elif defined(CONFIG_ARCH_ARMV4TE)
    #define FAST        __attribute__((optimize("O2")))
    #define FORCE_ARM   __attribute__((optimize("O2"), target("arm")))
    #define RAMFUNC     __attribute__((optimize("O2"), section(".ramsection"), long_call, noinline, noclone))
#endif

#endif // ATTRS_H
