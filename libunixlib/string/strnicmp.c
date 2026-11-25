/* strnicmp (), strncasecmp ()
 * Copyright (c) 2000-2011 UnixLib Developers
 */

#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <locale.h>
#include <internal/wordstr.h>

int
strnicmp (const char *s1, const char *s2, size_t n)
{
  int i, j;

  if (!n)
    return 0;

  /* 2026 (C5): equal bytes need no tolower (), and with both strings at
     the same alignment, equal words are skipped a word at a time.  Same
     results as before.  */
  const int same = __UL_SAMEALIGN (s1, s2);
  do
    {
      if (same && __UL_ALIGNED (s1) && n > 4)
	{
	  const __ul_word_t *w1 = (const __ul_word_t *) s1;
	  const __ul_word_t *w2 = (const __ul_word_t *) s2;
	  while (n > 4 && *w1 == *w2 && !__UL_HASZERO (*w1))
	    w1++, w2++, n -= 4;
	  s1 = (const char *) w1;
	  s2 = (const char *) w2;
	}
      i = (unsigned char) *s1++, j = (unsigned char) *s2++;
      if (i == j)
	{
	  if (i == 0)
	    return 0;
	  continue;
	}
      i = tolower (i);
      j = tolower (j);
      if (i != j || i == 0)
	return i - j;
    }
  while (--n);
  return 0;
}
strong_alias (strnicmp, strncasecmp)
