/* strcspn ()
 * Copyright (c) 2000-2006 UnixLib Developers
 */

#include <string.h>
#include <internal/byteset.h>

/* The internal name: a program may define its own strchrnul.  */
extern char *__strchrnul (const char *__s, int __c);

/* C8: after the first bytes, one lookup per byte in a 256-bit map of the set (the terminator
   included, so it stops there too), instead of a scan of the set for
   every byte.  A one-byte set uses strchrnul.  */
size_t
strcspn (const char *s1, const char *s)
{
  const unsigned char *p = (const unsigned char *) s1;
  const unsigned char *set = (const unsigned char *) s;

  if (set[0] == 0)
    return strlen (s1);
  if (set[1] == 0)
    return __strchrnul (s1, set[0]) - s1;

  for (int i = 0; i < __UL_BS_FIRST; i++, p++)
    if (*p == 0 || __ul_inset (*p, set))
      return p - (const unsigned char *) s1;

  __ul_byteset bs;
  __ul_byteset_make (&bs, set);
  bs.w[0] |= 1;
  while (!__UL_BS_HAS (&bs, *p))
    p++;
  return p - (const unsigned char *) s1;
}
