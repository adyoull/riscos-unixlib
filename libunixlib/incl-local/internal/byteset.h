/* A set of byte values as a 256-bit map, for strspn, strcspn and strpbrk.
   Copyright (c) 2026 UnixLib Developers.  */

#ifndef __INTERNAL_BYTESET_H
#define __INTERNAL_BYTESET_H

typedef struct
{
  unsigned int w[8];
} __ul_byteset;

#define __UL_BS_HAS(bs, c) (((bs)->w[(c) >> 5] >> ((c) & 31)) & 1u)

/* Clear BS, then add every byte of the string SET (not its terminator).  */
static inline void
__ul_byteset_make (__ul_byteset *bs, const unsigned char *set)
{
  unsigned int c;

  for (int i = 0; i < 8; i++)
    bs->w[i] = 0;
  while ((c = *set++) != 0)
    bs->w[c >> 5] |= 1u << (c & 31);
}

/* Short spans are the common case (skipping a space or two), and making
   the map costs about 90 instructions, so the first 8 bytes are scanned
   the old way and the map is made only for a longer span.  With 8, a
   span of 1 costs 10 instructions more than before, and a long span
   with a 5-byte set costs half.  */
#define __UL_BS_FIRST 8

/* Non-zero if byte C is in the string SET (never true for 0).  */
static inline int
__ul_inset (unsigned int c, const unsigned char *set)
{
  unsigned int d;

  while ((d = *set++) != 0)
    if (d == c)
      return 1;
  return 0;
}

#endif
