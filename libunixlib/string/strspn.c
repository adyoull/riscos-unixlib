/* strspn ()
 * Copyright (c) 2000-2006 UnixLib Developers
 */

#include <string.h>
#include <internal/byteset.h>

/* C8: after the first bytes, one lookup per byte in a 256-bit map of the set, instead of a
   scan of the set for every byte.  */
size_t
strspn (const char *s1, const char *s)
{
  const unsigned char *p = (const unsigned char *) s1;
  const unsigned char *set = (const unsigned char *) s;

  if (set[0] == 0)
    return 0;
  if (set[1] == 0)
    {
      unsigned int c = set[0];
      while (*p == c)
	p++;
      return p - (const unsigned char *) s1;
    }

  for (int i = 0; i < __UL_BS_FIRST; i++, p++)
    if (!__ul_inset (*p, set))
      return p - (const unsigned char *) s1;

  __ul_byteset bs;
  __ul_byteset_make (&bs, set);
  while (__UL_BS_HAS (&bs, *p))
    p++;
  return p - (const unsigned char *) s1;
}
