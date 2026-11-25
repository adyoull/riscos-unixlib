/* memchr ()
 * Copyright (c) 2000-2008 UnixLib Developers
 *
 * 2026 (C5): a word at a time once aligned (it was about 2 instructions
 * a byte).  Same results as before.
 */

#include <string.h>
#include <internal/wordstr.h>

void *
memchr (const void *s, int c_, size_t n)
{
  const unsigned char *p = s;
  unsigned int c = (unsigned char) c_;

  while (n != 0 && !__UL_ALIGNED (p))
    {
      if (*p == c)
	return (void *) p;
      p++, n--;
    }

  const __ul_word_t *w = (const __ul_word_t *) p;
  unsigned int pattern = c * __UL_ONES;
  /* Two words a time, then one.  The second word is only read once the
     first has no match: a caller may give an N larger than the object
     (POSIX: memchr stops at the first match), so nothing past the word
     holding the match is read (review, 2026-11-25).  */
  while (n >= 8)
    {
      if (__UL_HASZERO (w[0] ^ pattern))
	break;
      if (__UL_HASZERO (w[1] ^ pattern))
	{
	  w++, n -= 4;
	  break;
	}
      w += 2, n -= 8;
    }
  if (n >= 4 && !__UL_HASZERO (*w ^ pattern))
    w++, n -= 4;

  for (p = (const unsigned char *) w; n != 0; p++, n--)
    if (*p == c)
      return (void *) p;
  return NULL;
}
