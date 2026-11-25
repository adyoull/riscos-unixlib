/* strncpy ()
 * Copyright (c) 2000-2007 UnixLib Developers
 */

#include <string.h>
#include <internal/wordstr.h>

/* 2026 (C9): a word at a time while both strings have the same alignment
   and the word holds no terminator, then a byte at a time; the padding
   after the terminator is one memset.  Exactly the old results.  A
   source word is read only when at least 4 of the N bytes are left and
   it is aligned, so nothing is read outside the N bytes, nor past the
   aligned word that holds the terminator.  */
char *
strncpy (char *s, const char *s2, size_t n)
{
  unsigned char *d = (unsigned char *) s;
  const unsigned char *p = (const unsigned char *) s2;

  if (__UL_SAMEALIGN (d, p))
    {
      while (n != 0 && !__UL_ALIGNED (p))
	{
	  n--;
	  if ((*d++ = *p++) == 0)
	    goto pad;
	}
      __ul_word_t *wd = (__ul_word_t *) d;
      const __ul_word_t *ws = (const __ul_word_t *) p;
      while (n >= 4)
	{
	  unsigned int v = *ws;
	  if (__UL_HASZERO (v))
	    break;
	  *wd++ = v;
	  ws++;
	  n -= 4;
	}
      d = (unsigned char *) wd;
      p = (const unsigned char *) ws;
    }

  while (n != 0)
    {
      n--;
      if ((*d++ = *p++) == 0)
	goto pad;
    }
  return s;

pad:
  memset (d, 0, n);
  return s;
}
