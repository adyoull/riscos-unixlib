#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>
#define restrict
#define swprintf ul_swprintf
#define wcsftime ul_wcsftime
/* 2026: narrow a wide format string for the narrow functions.  Only
   Latin-1 can be narrowed: a character above 0xFF used to be cut to its
   low byte, so U+FF25 (fullwidth E) and others became '%' and started a
   conversion with no argument behind it.  Returns NULL (errno EILSEQ or
   ENOMEM) for such a format.  */
static char *
narrow_format (const wchar_t *format)
{
  size_t flen = wcslen (format), i;
  char *nfmt;

  for (i = 0; i < flen; i++)
    if ((unsigned long) format[i] > 0xFF)
      {
	errno = EILSEQ;
	return NULL;
      }
  if ((nfmt = malloc (flen + 1)) == NULL)
    return NULL;
  for (i = 0; i <= flen; i++)
    nfmt[i] = (char) format[i];
  return nfmt;
}

/* swprintf: narrow the format (Latin-1 only, see narrow_format), format
   with vsnprintf, then widen the result.  */
int
swprintf (wchar_t *restrict s, size_t n, const wchar_t *restrict format, ...)
{
  char *nfmt, *out;
  size_t i, outsize;
  int len;
  va_list ap, ap2;

  if (n == 0)
    return -1;
  if ((nfmt = narrow_format (format)) == NULL)
    return -1;
  /* Measure first, so a large n (e.g. INT_MAX as "no limit") doesn't make
     the scratch buffer that large.  */
  va_start (ap, format);
  va_copy (ap2, ap);
  len = vsnprintf (NULL, 0, nfmt, ap);
  va_end (ap);
  if (len < 0)
    {
      va_end (ap2);
      free (nfmt);
      return -1;
    }
  outsize = (size_t) len + 1 < n ? (size_t) len + 1 : n;
  if ((out = malloc (outsize)) == NULL)
    {
      va_end (ap2);
      free (nfmt);
      return -1;
    }
  vsnprintf (out, outsize, nfmt, ap2);
  va_end (ap2);
  for (i = 0; i < outsize && out[i] != 0; i++)
    s[i] = (unsigned char) out[i];
  s[i < outsize ? i : outsize - 1] = 0;
  if ((size_t) len >= n)
    len = -1;
  free (nfmt);
  free (out);
  return len;
}

/* wcsftime: as swprintf.  Returns 0 for a format that isn't Latin-1.  */
size_t
wcsftime (wchar_t *restrict wcs, size_t maxsize,
	  const wchar_t *restrict format, const struct tm *restrict timeptr)
{
  size_t i, r;
  char *nfmt, *out;

  if (maxsize == 0)
    return 0;
  if ((nfmt = narrow_format (format)) == NULL)
    return 0;
  if ((out = malloc (maxsize)) == NULL)
    {
      free (nfmt);
      return 0;
    }
  r = strftime (out, maxsize, nfmt, timeptr);
  for (i = 0; i < r; i++)
    wcs[i] = (unsigned char) out[i];
  if (r < maxsize)
    wcs[r] = 0;
  free (nfmt);
  free (out);
  return r;
}

/* Byte-oriented wide stream I/O (Latin-1).  */
