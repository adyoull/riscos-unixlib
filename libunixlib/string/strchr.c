/* strchr ()
 * Copyright (c) 2000-2008 UnixLib Developers
 *
 * 2026 (C5): a word at a time once aligned (it was about 2.5
 * instructions a byte).  Same results as before; strchr (s, 0) finds the
 * terminator.
 */

#include <stdlib.h>
#include <string.h>
#include <internal/wordstr.h>

char *
strchr (const char *s, int c_)
{
  const unsigned char *p = (const unsigned char *) s;
  unsigned int c = (unsigned char) c_;

  while (!__UL_ALIGNED (p))
    {
      if (*p == c)
	return (char *) p;
      if (*p == 0)
	return NULL;
      p++;
    }

  const __ul_word_t *w = (const __ul_word_t *) p;
  unsigned int pattern = c * __UL_ONES;
  /* Two words a time: the first word is always safe to read (aligned,
     and the string goes on into it); the second only once the first has
     no terminator, so never past the word holding it.  */
  for (;;)
    {
      unsigned int v = w[0];
      if (__UL_HASZERO (v) | __UL_HASZERO (v ^ pattern))
	break;
      v = w[1];
      if (__UL_HASZERO (v) | __UL_HASZERO (v ^ pattern))
	{
	  w++;
	  break;
	}
      w += 2;
    }

  for (p = (const unsigned char *) w; ; p++)
    {
      if (*p == c)
	return (char *) p;
      if (*p == 0)
	return NULL;
    }
}
strong_alias (strchr, index)
