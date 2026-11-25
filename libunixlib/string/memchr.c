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
  /* Two words a time, then one.  */
  const __ul_word_t *end = w + (n >> 3) * 2;
  while (w != end)
    {
      unsigned int a = w[0] ^ pattern, b = w[1] ^ pattern;
      if (__UL_HASZERO (a) | __UL_HASZERO (b))
	break;
      w += 2;
    }
  n -= (const unsigned char *) w - p;
  if (n >= 4 && !__UL_HASZERO (*w ^ pattern))
    w++, n -= 4;

  for (p = (const unsigned char *) w; n != 0; p++, n--)
    if (*p == c)
      return (void *) p;
  return NULL;
}
