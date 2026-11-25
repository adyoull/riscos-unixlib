/* Case insensitive string comparison.
   Copyright (c) 2002-2011 UnixLib Developers.  */

#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <locale.h>
#include <internal/wordstr.h>

int
stricmp (const char *s1, const char *s2)
{
  const unsigned char *p1 = (const unsigned char *) s1;
  const unsigned char *p2 = (const unsigned char *) s2;
  int result = 0;

  if (p1 == p2)
    return result;

  /* 2026 (C5): equal bytes need no case conversion (it was about 11
     instructions a byte), and with both strings at the same alignment,
     equal words are skipped a word at a time.  Same results as before.  */
  const int same = __UL_SAMEALIGN (p1, p2);
  for (;;)
    {
      if (same && __UL_ALIGNED (p1))
	{
	  const __ul_word_t *w1 = (const __ul_word_t *) p1;
	  const __ul_word_t *w2 = (const __ul_word_t *) p2;
	  while (*w1 == *w2 && !__UL_HASZERO (*w1))
	    w1++, w2++;
	  p1 = (const unsigned char *) w1;
	  p2 = (const unsigned char *) w2;
	}
      unsigned int c1 = *p1++, c2 = *p2++;
      if (c1 == c2)
	{
	  if (c1 == '\0')
	    return 0;
	  continue;
	}
      result = tolower (c1) - tolower (c2);
      if (result != 0 || c1 == '\0')
	return result;
    }
}
strong_alias (stricmp, strcasecmp)
