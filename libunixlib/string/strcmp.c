/* strcmp ()
 * Copyright (c) 2000-2007 UnixLib Developers
 *
 * 2026 (C5): a word at a time when both strings have the same alignment
 * (it was about 6 instructions a byte).  Same results as before: the
 * difference of the first bytes that differ, as unsigned chars.
 */

#include <string.h>
#include <internal/wordstr.h>

int
strcmp (const char *s1, const char *s2)
{
  const unsigned char *p1 = (const unsigned char *) s1;
  const unsigned char *p2 = (const unsigned char *) s2;

  if (__UL_SAMEALIGN (p1, p2))
    {
      while (!__UL_ALIGNED (p1))
	{
	  unsigned int c1 = *p1, c2 = *p2;
	  if (c1 != c2 || c1 == 0)
	    return (int) c1 - (int) c2;
	  p1++, p2++;
	}
      const __ul_word_t *w1 = (const __ul_word_t *) p1;
      const __ul_word_t *w2 = (const __ul_word_t *) p2;
      for (;;)
	{
	  unsigned int a = *w1, b = *w2;
	  if (a != b || __UL_HASZERO (a))
	    break;
	  w1++, w2++;
	}
      p1 = (const unsigned char *) w1;
      p2 = (const unsigned char *) w2;
    }

  for (;;)
    {
      unsigned int c1 = *p1, c2 = *p2;
      if (c1 != c2 || c1 == 0)
	return (int) c1 - (int) c2;
      p1++, p2++;
    }
}
