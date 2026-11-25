/* memcmp (), bcmp ()
 * Copyright (c) 2000-2008 UnixLib Developers
 *
 * 2026 (C5): a word at a time when both blocks have the same alignment
 * (it was about 5 instructions a byte).  Same results as before: the
 * difference of the first bytes that differ.
 */

#include <string.h>
#include <strings.h>
#include <internal/wordstr.h>

int
memcmp (const void *s1, const void *s2, size_t n)
{
  const unsigned char *p1 = s1;
  const unsigned char *p2 = s2;

  if (__UL_SAMEALIGN (p1, p2))
    {
      while (n != 0 && !__UL_ALIGNED (p1))
	{
	  if (*p1 != *p2)
	    return *p1 - *p2;
	  p1++, p2++, n--;
	}
      const __ul_word_t *w1 = (const __ul_word_t *) p1;
      const __ul_word_t *w2 = (const __ul_word_t *) p2;
      while (n >= 16 && w1[0] == w2[0] && w1[1] == w2[1]
	     && w1[2] == w2[2] && w1[3] == w2[3])
	w1 += 4, w2 += 4, n -= 16;
      while (n >= 4 && *w1 == *w2)
	w1++, w2++, n -= 4;
      p1 = (const unsigned char *) w1;
      p2 = (const unsigned char *) w2;
    }

  for (; n != 0; p1++, p2++, n--)
    if (*p1 != *p2)
      return *p1 - *p2;
  return 0;
}
strong_alias (memcmp, bcmp)
