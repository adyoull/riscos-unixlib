#ifndef FAKE_SWIS_H
#define FAKE_SWIS_H
#include "kernel.h"
#define _IN(i)      (1U << (i))
#define _INR(a,b)   ((~0U << (a)) & ~(~0U << ((b) + 1)))
#define _OUT(i)     (1U << (31 - (i)))
#define _OUTR(a,b)  ((((1U << ((b) - (a) + 1)) - 1)) << (31 - (b)))
#define OS_SWINumberFromString 0x39
#define OS_Module 0x1e
const _kernel_oserror *_swix (int swi, unsigned mask, ...);
#endif
