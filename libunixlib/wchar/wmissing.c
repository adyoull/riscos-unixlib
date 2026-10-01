/* Wide character functions that UnixLib previously stubbed out with
   "Not implemented" + abort().  These are simple implementations that
   are correct for the 8-bit range UnixLib's ctype tables cover, which is
   what libstdc++'s generic locale model needs (it calls wctype() and
   iswctype() while initialising std::locale).
   2026 Andrew Youll.  */

#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <wchar.h>
#include <wctype.h>

#define NARROW(wc) ((wc) >= 0 && (wc) < 256)

enum { WT_NONE, WT_ALNUM, WT_ALPHA, WT_BLANK, WT_CNTRL, WT_DIGIT, WT_GRAPH,
       WT_LOWER, WT_PRINT, WT_PUNCT, WT_SPACE, WT_UPPER, WT_XDIGIT };

int iswupper (wint_t wc) { return NARROW (wc) ? isupper (wc) : 0; }
int iswupper_l (wint_t wc, locale_t l) { (void) l; return iswupper (wc); }
int iswlower (wint_t wc) { return NARROW (wc) ? islower (wc) : 0; }
int iswlower_l (wint_t wc, locale_t l) { (void) l; return iswlower (wc); }
int iswblank (wint_t wc) { return wc == L' ' || wc == L'\t'; }
int iswblank_l (wint_t wc, locale_t l) { (void) l; return iswblank (wc); }

wctype_t
wctype (const char *name)
{
  static const char *const names[] = { "alnum", "alpha", "blank", "cntrl",
    "digit", "graph", "lower", "print", "punct", "space", "upper", "xdigit" };
  unsigned i;
  if (name == NULL)
    return WT_NONE;
  for (i = 0; i < sizeof (names) / sizeof (names[0]); i++)
    if (strcmp (name, names[i]) == 0)
      return (wctype_t) (i + 1);
  return WT_NONE;
}

wctype_t wctype_l (const char *name, locale_t l) { (void) l; return wctype (name); }

int
iswctype (wint_t wc, wctype_t desc)
{
  switch (desc)
    {
    case WT_ALNUM: return iswalnum (wc);
    case WT_ALPHA: return iswalpha (wc);
    case WT_BLANK: return iswblank (wc);
    case WT_CNTRL: return iswcntrl (wc);
    case WT_DIGIT: return iswdigit (wc);
    case WT_GRAPH: return iswgraph (wc);
    case WT_LOWER: return iswlower (wc);
    case WT_PRINT: return iswprint (wc);
    case WT_PUNCT: return iswpunct (wc);
    case WT_SPACE: return iswspace (wc);
    case WT_UPPER: return iswupper (wc);
    case WT_XDIGIT: return iswxdigit (wc);
    default: return 0;
    }
}

int iswctype_l (wint_t wc, wctype_t d, locale_t l) { (void) l; return iswctype (wc, d); }

int wcscoll (const wchar_t *s1, const wchar_t *s2) { return wcscmp (s1, s2); }
int wcscoll_l (const wchar_t *s1, const wchar_t *s2, locale_t l) { (void) l; return wcscmp (s1, s2); }

size_t
wcsxfrm (wchar_t *restrict s1, const wchar_t *restrict s2, size_t n)
{
  size_t len = wcslen (s2);
  if (n > len)
    wcscpy (s1, s2);
  return len;
}

size_t wcsxfrm_l (wchar_t *s1, const wchar_t *s2, size_t n, locale_t l) { (void) l; return wcsxfrm (s1, s2, n); }

int
wcsncasecmp (const wchar_t *s1, const wchar_t *s2, size_t n)
{
  for (; n; n--, s1++, s2++)
    {
      wint_t a = towlower (*s1), b = towlower (*s2);
      if (a != b)
	return a < b ? -1 : 1;
      if (a == 0)
	break;
    }
  return 0;
}

int wcscasecmp (const wchar_t *s1, const wchar_t *s2) { return wcsncasecmp (s1, s2, (size_t) -1); }
int wcscasecmp_l (const wchar_t *s1, const wchar_t *s2, locale_t l) { (void) l; return wcscasecmp (s1, s2); }
int wcsncasecmp_l (const wchar_t *s1, const wchar_t *s2, size_t n, locale_t l) { (void) l; return wcsncasecmp (s1, s2, n); }

/* Numeric conversions: narrow the (ASCII) number text, convert, and map
   the end pointer back.  */
#define NUMBUF 128
static size_t
narrow_num (const wchar_t *src, char *dst)
{
  size_t i;
  for (i = 0; i < NUMBUF - 1 && src[i] != 0 && src[i] < 128; i++)
    dst[i] = (char) src[i];
  dst[i] = 0;
  return i;
}

#define WCSTO(name, type, call)						\
  type name (const wchar_t *restrict nptr, wchar_t **restrict endptr, int base) \
  {									\
    char buf[NUMBUF], *end;						\
    type r;								\
    narrow_num (nptr, buf);						\
    r = call (buf, &end, base);						\
    if (endptr)								\
      *endptr = (wchar_t *) nptr + (end - buf);				\
    return r;								\
  }
WCSTO (wcstol, long int, strtol)
WCSTO (wcstoul, unsigned long int, strtoul)
WCSTO (wcstoll, long long int, strtoll)
WCSTO (wcstoull, unsigned long long int, strtoull)

#define WCSTOF(name, type, call)					\
  type name (const wchar_t *restrict nptr, wchar_t **restrict endptr)	\
  {									\
    char buf[NUMBUF], *end;						\
    type r;								\
    narrow_num (nptr, buf);						\
    r = call (buf, &end);						\
    if (endptr)								\
      *endptr = (wchar_t *) nptr + (end - buf);				\
    return r;								\
  }
WCSTOF (wcstod, double, strtod)
WCSTOF (wcstof, float, strtof)
WCSTOF (wcstold, long double, strtold)

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
wint_t
putwc (wchar_t wc, FILE *stream)
{
  if (wc < 0 || wc > 255)
    return WEOF;
  return fputc ((int) wc, stream) == EOF ? WEOF : (wint_t) wc;
}

wint_t
getwc (FILE *stream)
{
  int c = fgetc (stream);
  return c == EOF ? WEOF : (wint_t) c;
}

wint_t
ungetwc (wint_t wc, FILE *stream)
{
  if (wc == WEOF || wc > 255)
    return WEOF;
  return ungetc ((int) wc, stream) == EOF ? WEOF : wc;
}
