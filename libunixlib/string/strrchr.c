/* strrchr ()
 * Copyright (c) 2000-2008 UnixLib Developers
 */

#include <string.h>
#include <internal/wordstr.h>

/* A word at a time (C8): a word with neither the terminator nor C is
   skipped; one with C is searched for its last C.  Only aligned words
   are read, so never past the word that holds the terminator.  */
char *
strrchr (const char *s, int c_)
{
  const unsigned char *p = (const unsigned char *) s;
  const unsigned char *last = NULL;
  unsigned int c = (unsigned char) c_;

  if (c == 0)
    return (char *) s + strlen (s);

  while (!__UL_ALIGNED (p))
    {
      if (*p == c)
	last = p;
      if (*p == 0)
	return (char *) last;
      p++;
    }

  const __ul_word_t *w = (const __ul_word_t *) p;
  unsigned int pattern = c * __UL_ONES;
  for (;;)
    {
      unsigned int v = *w;
      if (__UL_HASZERO (v))
	break;
      if (__UL_HASZERO (v ^ pattern))
	{
	  const unsigned char *b = (const unsigned char *) w;
	  if (b[3] == c)
	    last = b + 3;
	  else if (b[2] == c)
	    last = b + 2;
	  else if (b[1] == c)
	    last = b + 1;
	  else
	    last = b;
	}
      w++;
    }

  for (p = (const unsigned char *) w; ; p++)
    {
      if (*p == c)
	last = p;
      if (*p == 0)
	return (char *) last;
    }
}
strong_alias (strrchr, rindex)
