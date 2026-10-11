/* Host test for UnixLib's scanf length modifiers (2026, C3): ll, hh, j, q,
   z, t, %Lf and %Ld.  Before, only l, L and h were known: %lld stored a
   long, %hhd a short, %jd/%zu/%td/%qd weren't understood, %Lf stored a
   float.  */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <stdarg.h>
int ul_vfscanf (FILE *, const char *, va_list);

static int ul_sscanf (const char *in, const char *fmt, ...)
{
  FILE *f = fmemopen ((void *) in, strlen (in), "r");
  va_list ap;
  va_start (ap, fmt);
  int r = ul_vfscanf (f, fmt, ap);
  va_end (ap);
  fclose (f);
  return r;
}

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
  printf ("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main (void)
{
  long long ll = 0;
  unsigned long long ull = 0;
  struct { signed char c; unsigned char guard; } hh = { 0, 0x5A };
  intmax_t j = 0;
  size_t z = 0;
  ptrdiff_t t = 0;
  long double ld = 0;
  int n;

  CHECK (ul_sscanf ("-9000000000", "%lld", &ll) == 1 && ll == -9000000000LL);
  CHECK (ul_sscanf ("ffffffffffffffff", "%llx", &ull) == 1 && ull == ~0ULL);
  CHECK (ul_sscanf ("-5", "%hhd", &hh.c) == 1 && hh.c == -5 && hh.guard == 0x5A);
  CHECK (ul_sscanf ("123456789012", "%jd", &j) == 1 && j == 123456789012LL);
  CHECK (ul_sscanf ("4096", "%zu", &z) == 1 && z == 4096);
  CHECK (ul_sscanf ("-77", "%td", &t) == 1 && t == -77);
  ll = 0;
  CHECK (ul_sscanf ("8000000000", "%qd", &ll) == 1 && ll == 8000000000LL);
  ll = 0;
  CHECK (ul_sscanf ("5000000000", "%Ld", &ll) == 1 && ll == 5000000000LL);
  CHECK (ul_sscanf ("2.5", "%Lf", &ld) == 1 && ld == 2.5L);
  ll = 0;
  CHECK (ul_sscanf ("1 2", "%*lld %lld%n", &ll, &n) == 1 && ll == 2 && n == 3);
  long long nn = -1;
  CHECK (ul_sscanf ("abc", "abc%lln", &nn) == 0 && nn == 3);
  /* Unchanged: plain, l, h.  */
  int i = 0; long l = 0; short s = 0;
  CHECK (ul_sscanf ("7 8 9", "%d %ld %hd", &i, &l, &s) == 3 && i == 7 && l == 8 && s == 9);

  printf ("scanf: %d checks, %d failed\n", checks, failures);
  return failures != 0;
}
