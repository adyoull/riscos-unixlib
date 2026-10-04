/* Wide character functions that UnixLib previously stubbed out with
   "Not implemented" + abort().  These are simple implementations that
   are correct for the 8-bit range UnixLib's ctype tables cover, which is
   what libstdc++'s generic locale model needs (it calls wctype() and
   iswctype() while initialising std::locale).
   2026 Andrew Youll.  */

#include <limits.h>
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
   the end pointer back.  2026: the whole ASCII run is narrowed; it used to
   be cut at 127 characters, so a longer number (leading zeros, a long
   hex float) was converted wrongly.  Short runs use the stack buffer,
   longer ones are allocated (falling back to the first 127 characters
   if that fails).  */
#define NUMBUF 128
static char *
narrow_num (const wchar_t *src, char *buf)
{
  size_t i, len = 0;
  char *dst = buf;

  while (src[len] != 0 && (unsigned long) src[len] < 128)
    len++;
  if (len >= NUMBUF)
    {
      dst = malloc (len + 1);
      if (dst == NULL)
	{
	  dst = buf;
	  len = NUMBUF - 1;
	}
    }
  for (i = 0; i < len; i++)
    dst[i] = (char) src[i];
  dst[len] = 0;
  return dst;
}

#define WCSTO(name, type, call)						\
  type name (const wchar_t *restrict nptr, wchar_t **restrict endptr, int base) \
  {									\
    char buf[NUMBUF], *str, *end;					\
    type r;								\
    str = narrow_num (nptr, buf);					\
    r = call (str, &end, base);						\
    if (endptr)								\
      *endptr = (wchar_t *) nptr + (end - str);				\
    if (str != buf)							\
      free (str);							\
    return r;								\
  }
WCSTO (wcstol, long int, strtol)
WCSTO (wcstoul, unsigned long int, strtoul)
WCSTO (wcstoll, long long int, strtoll)
WCSTO (wcstoull, unsigned long long int, strtoull)

#define WCSTOF(name, type, call)					\
  type name (const wchar_t *restrict nptr, wchar_t **restrict endptr)	\
  {									\
    char buf[NUMBUF], *str, *end;					\
    type r;								\
    str = narrow_num (nptr, buf);					\
    r = call (str, &end);						\
    if (endptr)								\
      *endptr = (wchar_t *) nptr + (end - str);				\
    if (str != buf)							\
      free (str);							\
    return r;								\
  }
WCSTOF (wcstod, double, strtod)
WCSTOF (wcstof, float, strtof)
WCSTOF (wcstold, long double, strtold)

/* 2026: swprintf and wcsftime work through the narrow functions in
   UTF-8, UnixLib's multibyte encoding (wcrtomb and mbrtowc are always
   UTF-8): the format is narrowed to UTF-8, vsnprintf turns %ls and %lc
   arguments into UTF-8 itself, and the result is widened back from
   UTF-8.  (Narrowing to Latin-1 and widening byte by byte, as 5.0.3.1
   did, made %ls and %lc arguments come out as their UTF-8 bytes, one
   character each: the 2026-10-04 audit.)  A byte that isn't part of a
   valid UTF-8 sequence (a narrow %s argument in Latin-1, say) is taken as
   Latin-1, as before.  */

/* Narrow FORMAT to UTF-8.  NULL with errno EILSEQ for a character that
   isn't Unicode (a surrogate, or above 0x10FFFF), or ENOMEM.  UTF-8 bytes
   are all 0x80 or above, so none can be taken for a '%'.  */
static char *
narrow_format (const wchar_t *format)
{
  size_t flen = wcslen (format), i, o = 0;
  char *nfmt;

  for (i = 0; i < flen; i++)
    {
      unsigned long c = (unsigned long) format[i];
      if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
	{
	  errno = EILSEQ;
	  return NULL;
	}
    }
  if ((nfmt = malloc (flen * 4 + 1)) == NULL)
    return NULL;
  for (i = 0; i < flen; i++)
    {
      unsigned long c = (unsigned long) format[i];
      if (c < 0x80)
	nfmt[o++] = (char) c;
      else if (c < 0x800)
	{
	  nfmt[o++] = (char) (0xC0 | (c >> 6));
	  nfmt[o++] = (char) (0x80 | (c & 0x3F));
	}
      else if (c < 0x10000)
	{
	  nfmt[o++] = (char) (0xE0 | (c >> 12));
	  nfmt[o++] = (char) (0x80 | ((c >> 6) & 0x3F));
	  nfmt[o++] = (char) (0x80 | (c & 0x3F));
	}
      else
	{
	  nfmt[o++] = (char) (0xF0 | (c >> 18));
	  nfmt[o++] = (char) (0x80 | ((c >> 12) & 0x3F));
	  nfmt[o++] = (char) (0x80 | ((c >> 6) & 0x3F));
	  nfmt[o++] = (char) (0x80 | (c & 0x3F));
	}
    }
  nfmt[o] = '\0';
  return nfmt;
}

/* Widen LEN bytes of IN from UTF-8, storing at most MAX characters in OUT
   (OUT may be NULL); returns the number of characters in all of IN.
   Invalid, overlong or truncated sequences and surrogates are taken a
   byte at a time as Latin-1.  */
static size_t
widen_utf8 (const char *in, size_t len, wchar_t *out, size_t max)
{
  const unsigned char *p = (const unsigned char *) in;
  size_t i = 0, n = 0;

  while (i < len)
    {
      unsigned long c = p[i];
      size_t k = 0;

      if (c >= 0xC2 && c <= 0xDF)
	k = 1, c &= 0x1F;
      else if (c >= 0xE0 && c <= 0xEF)
	k = 2, c &= 0x0F;
      else if (c >= 0xF0 && c <= 0xF4)
	k = 3, c &= 0x07;
      if (k)
	{
	  size_t j;
	  for (j = 1; j <= k; j++)
	    {
	      if (i + j >= len || (p[i + j] & 0xC0) != 0x80)
		break;
	      c = (c << 6) | (p[i + j] & 0x3F);
	    }
	  if (j > k
	      && !(k == 2 && c < 0x800)
	      && !(k == 3 && (c < 0x10000 || c > 0x10FFFF))
	      && !(c >= 0xD800 && c <= 0xDFFF))
	    {
	      if (out && n < max)
		out[n] = (wchar_t) c;
	      n++;
	      i += k + 1;
	      continue;
	    }
	}
      if (out && n < max)
	out[n] = (wchar_t) p[i];
      n++;
      i++;
    }
  return n;
}

/* swprintf: narrow the format, format with vsnprintf, widen the result
   (see above).  As glibc: -1 if the result doesn't fit in N characters
   (the first N - 1 are stored, then a terminator).  2026 (audit): S is
   terminated on every error too, when N isn't 0.  */
int
swprintf (wchar_t *restrict s, size_t n, const wchar_t *restrict format, ...)
{
  char *nfmt, *out;
  size_t wlen;
  int len;
  va_list ap, ap2;

  if (n == 0)
    return -1;
  s[0] = 0;
  if ((nfmt = narrow_format (format)) == NULL)
    return -1;
  /* Measure first, then format into a buffer just big enough (a large n,
     e.g. INT_MAX as "no limit", mustn't make the buffer that large).  */
  va_start (ap, format);
  va_copy (ap2, ap);
  len = vsnprintf (NULL, 0, nfmt, ap);
  va_end (ap);
  if (len < 0 || (out = malloc ((size_t) len + 1)) == NULL)
    {
      va_end (ap2);
      free (nfmt);
      return -1;
    }
  vsnprintf (out, (size_t) len + 1, nfmt, ap2);
  va_end (ap2);
  wlen = widen_utf8 (out, (size_t) len, s, n - 1);
  s[wlen < n ? wlen : n - 1] = 0;
  free (nfmt);
  free (out);
  if (wlen >= n || wlen > INT_MAX)
    return -1;
  return (int) wlen;
}

/* wcsftime: as swprintf.  Returns 0 for a format that isn't Unicode, or
   if the result (with its terminator) doesn't fit in MAXSIZE characters.
   2026 (audit): the scratch buffer grows as needed instead of being
   MAXSIZE bytes (a huge MAXSIZE as "no limit" made it fail).  */
size_t
wcsftime (wchar_t *restrict wcs, size_t maxsize,
	  const wchar_t *restrict format, const struct tm *restrict timeptr)
{
  size_t r = 0, w, size, limit;
  char *nfmt, *out = NULL;

  if (maxsize == 0)
    return 0;
  wcs[0] = 0;
  if ((nfmt = narrow_format (format)) == NULL)
    return 0;
  if (nfmt[0] == '\0')
    {
      free (nfmt);
      return 0;
    }
  /* A wide character is at most 4 UTF-8 bytes.  strftime returns 0 both
     for "too small" and for an empty result, so stop at a sane size.  */
  limit = maxsize < ((size_t) 1 << 18) ? maxsize * 4 : (size_t) 1 << 20;
  for (size = 256; ; size *= 2)
    {
      char *bigger;
      if (size > limit)
	size = limit;
      if ((bigger = realloc (out, size)) == NULL)
	break;
      out = bigger;
      if ((r = strftime (out, size, nfmt, timeptr)) != 0 || size >= limit)
	break;
    }
  w = r ? widen_utf8 (out, r, NULL, 0) : 0;
  if (r == 0 || w >= maxsize)
    w = 0;
  else
    {
      widen_utf8 (out, r, wcs, w);
      wcs[w] = 0;
    }
  free (nfmt);
  free (out);
  return w;
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
