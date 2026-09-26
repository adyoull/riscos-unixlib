/* iswalnum(), iswalpha(), iswcntrl(), iswdigit(), iswgraph(), towlower(),
 * iswprint(), iswpunct(), iswspace(), towupper(), iswxdigit()
 * Copyright (c) 2010 UnixLib Developers
 * 2026: guard against wide characters outside the 8-bit ctype tables.
 */

#include <ctype.h>
#include <wctype.h>

#define NARROW(wc) ((wc) >= 0 && (wc) < 256)

int iswalnum (wint_t wc) { return NARROW (wc) ? isalnum (wc) : 0; }
int iswalpha (wint_t wc) { return NARROW (wc) ? isalpha (wc) : 0; }
int iswcntrl (wint_t wc) { return NARROW (wc) ? iscntrl (wc) : 0; }
int iswdigit (wint_t wc) { return NARROW (wc) ? isdigit (wc) : 0; }
int iswgraph (wint_t wc) { return NARROW (wc) ? isgraph (wc) : 0; }
wint_t towlower (wint_t wc) { return NARROW (wc) ? (wint_t) tolower (wc) : wc; }
int iswprint (wint_t wc) { return NARROW (wc) ? isprint (wc) : 0; }
int iswpunct (wint_t wc) { return NARROW (wc) ? ispunct (wc) : 0; }
int iswspace (wint_t wc) { return NARROW (wc) ? isspace (wc) : 0; }
wint_t towupper (wint_t wc) { return NARROW (wc) ? (wint_t) toupper (wc) : wc; }
int iswxdigit (wint_t wc) { return NARROW (wc) ? isxdigit (wc) : 0; }
