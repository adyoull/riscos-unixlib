/* UnixLib global variable.
   Copyright (c) 2011 UnixLib Developers.  */

#include <stddef.h>
#include <internal/unix.h>

struct ul_global __ul_global;

/* 2026: the assembler reaches pvops as GBL_PVOPS (incl-local/internal/
   asm_dec.s); keep the two in step.  */
#if !defined(__SOFTFP__) && defined(__VFP_FP__)
_Static_assert (offsetof (struct ul_global, pvops) == 124, "GBL_PVOPS");
#else
_Static_assert (offsetof (struct ul_global, pvops) == 120, "GBL_PVOPS");
#endif

