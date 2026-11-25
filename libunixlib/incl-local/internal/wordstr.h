/* Word-at-a-time helpers for the string functions (2026, C5).

   Only aligned words are read, so a read never crosses into the next page
   (a word holding a string's last byte is on that byte's page).  No
   unaligned loads: RISC OS may fault them.  */
#ifndef __INTERNAL_WORDSTR_H
#define __INTERNAL_WORDSTR_H

#include <stdint.h>

typedef unsigned int __attribute__ ((__may_alias__)) __ul_word_t;

#define __UL_ONES  0x01010101u
#define __UL_HIGHS 0x80808080u
/* Non-zero if any byte of V is 0 (exact, no false positives).  */
#define __UL_HASZERO(v) (((v) - __UL_ONES) & ~(v) & __UL_HIGHS)
#define __UL_ALIGNED(p) (((uintptr_t) (p) & 3) == 0)
#define __UL_SAMEALIGN(p, q) ((((uintptr_t) (p) ^ (uintptr_t) (q)) & 3) == 0)

#endif
