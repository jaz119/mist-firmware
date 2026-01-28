#ifndef ATTRS_H
#define ATTRS_H

#define RAMFUNC __attribute__ ((long_call, noinline, noclone, used, section(".ramsection"), optimize("O2")))
#define FAST __attribute__((noclone, optimize("O2")))

#endif // ATTRS_H
