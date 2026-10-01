/* iswalnum_l(), iswalpha_l(), iswcntrl_l(), iswdigit_l(), iswgraph_l(), towlower_l(),
 * iswprint_l(), iswpunct_l(), iswspace_l(), towupper_l(), iswxdigit_l()
 * Copyright (c) 2010 UnixLib Developers
 * 2026: guard against wide characters outside the 8-bit ctype tables, as
 * the non-locale versions in wctype.c do.
 */

#include <ctype.h>
#include <locale.h>
#include <wctype.h>

#define NARROW(wc) ((wc) >= 0 && (wc) < 256)

int
iswalnum_l (wint_t wc, locale_t locale)
{
  return NARROW (wc) ? isalnum_l (wc, locale) : 0;
}

int
iswalpha_l (wint_t wc, locale_t locale)
{
  return NARROW (wc) ? isalpha_l (wc, locale) : 0;
}

int
iswcntrl_l (wint_t wc, locale_t locale)
{
  return NARROW (wc) ? iscntrl_l (wc, locale) : 0;
}

int
iswdigit_l (wint_t wc, locale_t locale)
{
  return NARROW (wc) ? isdigit_l (wc, locale) : 0;
}

int
iswgraph_l (wint_t wc, locale_t locale)
{
  return NARROW (wc) ? isgraph_l (wc, locale) : 0;
}

int
iswprint_l (wint_t wc, locale_t locale)
{
  return NARROW (wc) ? isprint_l (wc, locale) : 0;
}

int
iswpunct_l (wint_t wc, locale_t locale)
{
  return NARROW (wc) ? ispunct_l (wc, locale) : 0;
}

int
iswspace_l (wint_t wc, locale_t locale)
{
  return NARROW (wc) ? isspace_l (wc, locale) : 0;
}

int
iswxdigit_l (wint_t wc, locale_t locale)
{
  return NARROW (wc) ? isxdigit_l (wc, locale) : 0;
}

wint_t
towlower_l (wint_t wc, locale_t locale)
{
  return NARROW (wc) ? (wint_t) tolower_l (wc, locale) : wc;
}

wint_t
towupper_l (wint_t wc, locale_t locale)
{
  return NARROW (wc) ? (wint_t) toupper_l (wc, locale) : wc;
}
