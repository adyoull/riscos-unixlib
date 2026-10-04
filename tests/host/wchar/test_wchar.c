/* Host tests for UnixLib's swprintf, wcsftime and wcsto* (wchar/wmissing.c),
   compiled for the PC with their names changed (ul_*) so they don't clash
   with the C library's.  */
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

int ul_swprintf (wchar_t *s, size_t n, const wchar_t *format, ...);
size_t ul_wcsftime (wchar_t *wcs, size_t maxsize, const wchar_t *format,
		    const struct tm *timeptr);
long ul_wcstol (const wchar_t *nptr, wchar_t **endptr, int base);
unsigned long long ul_wcstoull (const wchar_t *nptr, wchar_t **endptr, int base);
double ul_wcstod (const wchar_t *nptr, wchar_t **endptr);

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf ("FAIL %d: ", __LINE__); printf (__VA_ARGS__); printf ("\n"); } } while (0)

int
main (void)
{
  wchar_t buf[64];
  int r;

  /* UnixLib's wcrtomb is always UTF-8; the host's follows the locale.  */
  if (!setlocale (LC_ALL, "C.UTF-8"))
    setlocale (LC_ALL, "en_US.UTF-8");

  r = ul_swprintf (buf, 64, L"a%db%s", 42, "xy");
  CHECK (r == 6 && wcscmp (buf, L"a42bxy") == 0, "plain format (%d)", r);
  r = ul_swprintf (buf, 64, L"caf\xe9 %d", 1);
  CHECK (r == 6 && buf[3] == 0xe9, "Latin-1 literal kept (%d)", r);

  /* U+FF25 has low byte 0x25 ('%'): it used to start a conversion and read
     an argument that wasn't there (5.0.3.1 refused it; since the 2026-10-04
     audit any Unicode format character is kept as itself).  */
  r = ul_swprintf (buf, 64, L"\xff25" L"d|", 7);
  CHECK (r == 3 && buf[0] == 0xff25 && wcscmp (buf + 1, L"d|") == 0, "U+FF25 is a literal, not '%%' (%d)", r);
  r = ul_swprintf (buf, 64, L"x\x2025s");
  CHECK (r == 3 && buf[1] == 0x2025, "U+2025 isn't '%%' either (%d)", r);
  errno = 0;
  r = ul_swprintf (buf, 64, L"x\xd800");
  CHECK (r == -1 && errno == EILSEQ && buf[0] == 0, "a surrogate in the format: EILSEQ, buffer terminated (%d)", r);

  /* 2026-10-04 audit: %ls and %lc came out as UTF-8 bytes, one character
     each, and the count was in bytes.  */
  r = ul_swprintf (buf, 64, L"%lc|", (wint_t) 0xe9);
  CHECK (r == 2 && buf[0] == 0xe9 && buf[1] == L'|', "%%lc of U+00E9 (%d)", r);
  r = ul_swprintf (buf, 64, L"[%ls]", L"\xe9\x263a\x1f600");
  CHECK (r == 5 && buf[1] == 0xe9 && buf[2] == 0x263a && buf[3] == 0x1f600 && buf[4] == L']', "%%ls with 2-, 3- and 4-byte characters (%d)", r);
  r = ul_swprintf (buf, 3, L"%ls", L"\x263a\x263a\x263a");
  CHECK (r == -1 && buf[0] == 0x263a && buf[1] == 0x263a && buf[2] == 0, "truncated in characters, not bytes (%d)", r);
  /* A narrow %s in Latin-1 (not valid UTF-8) is still taken as Latin-1.  */
  r = ul_swprintf (buf, 64, L"%s", "caf\xe9");
  CHECK (r == 4 && buf[3] == 0xe9, "Latin-1 %%s byte kept (%d)", r);

  /* Truncation: -1, and the output is cut and terminated.  */
  r = ul_swprintf (buf, 4, L"%s", "abcdef");
  CHECK (r == -1 && wcscmp (buf, L"abc") == 0, "truncated (%d)", r);
  /* A huge n doesn't need a huge scratch buffer.  */
  r = ul_swprintf (buf, (size_t) 0x7fffffff, L"%d", 123);
  CHECK (r == 3 && wcscmp (buf, L"123") == 0, "n = INT_MAX works (%d)", r);

  struct tm tm = { .tm_year = 126, .tm_mon = 8, .tm_mday = 30, .tm_hour = 12 };
  size_t z = ul_wcsftime (buf, 64, L"%Y-%m-%d", &tm);
  CHECK (z == 10 && wcscmp (buf, L"2026-09-30") == 0, "wcsftime (%zu)", z);
  z = ul_wcsftime (buf, 64, L"\x263a%Y", &tm);
  CHECK (z == 5 && buf[0] == 0x263a && wcscmp (buf + 1, L"2026") == 0, "wcsftime keeps a non-Latin-1 literal (%zu)", z);
  z = ul_wcsftime (buf, 5, L"\x263a%Y", &tm);
  CHECK (z == 0, "wcsftime: 5 characters + terminator don't fit in 5 (%zu)", z);
  z = ul_wcsftime (buf, (size_t) -1 / sizeof (wchar_t), L"%Y", &tm);
  CHECK (z == 4 && wcscmp (buf, L"2026") == 0, "wcsftime with a huge maxsize (%zu)", z);

  /* wcsto*: a number longer than 127 characters used to be cut short.  */
  {
    wchar_t num[400], *end;
    int i;
    for (i = 0; i < 300; i++)
      num[i] = L'0';
    wcscpy (num + 300, L"12345x");
    long l = ul_wcstol (num, &end, 10);
    CHECK (l == 12345 && end == num + 305, "wcstol, 305 digits (%ld, end %d)", l, (int) (end - num));
    num[0] = L'1';
    unsigned long long u = ul_wcstoull (num, &end, 10);
    CHECK (u == ~0ULL && errno == ERANGE && end == num + 305, "wcstoull overflows (%llu)", u);
    wcscpy (num, L"  -12.5e1\x263a");
    double d = ul_wcstod (num, &end);
    CHECK (d == -125.0 && end == num + 9, "wcstod stops at non-ASCII (%g, end %d)", d, (int) (end - num));
    l = ul_wcstol (L"zz", &end, 10);
    CHECK (l == 0 && *end == L'z', "wcstol no digits");
  }

  printf ("wchar: %d checks, %d failed\n", checks, fails);
  return fails != 0;
}
