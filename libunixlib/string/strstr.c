/* strstr ()
 * Copyright (c) 2000-2007 UnixLib Developers
 */

#include <stdlib.h>
#include <string.h>

/* C8: jump to each copy of the needle's first byte with strchr (a word
   at a time), then compare the rest.  The haystack is no longer measured
   first, and the search stops as soon as the haystack is too short for
   a match.  */
char *
strstr (const char *s1, const char *s2)
{
  const unsigned char *h = (const unsigned char *) s1;
  const unsigned char *n = (const unsigned char *) s2;
  unsigned int first = n[0];

  if (first == 0)
    return (char *) s1;
  if (n[1] == 0)
    return strchr (s1, first);

  for (;;)
    {
      h = (const unsigned char *) strchr ((const char *) h, first);
      if (h == NULL)
	return NULL;

      size_t i = 1;
      while (n[i] != 0 && h[i] == n[i])
	i++;
      if (n[i] == 0)
	return (char *) h;
      if (h[i] == 0)
	return NULL;		/* Less haystack left than needle.  */
      h++;
    }
}
