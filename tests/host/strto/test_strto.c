/* Host test for C6 (stdlib/strtol.c, strtoul.c, strtoll.c, strtoull.c):
   the new versions give exactly the old ones' results, end pointers and
   errno, for every base 0 and 2-36 and strings around the limits.  */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

long old_strtol (const char *, char **, int), new_strtol (const char *, char **, int);
unsigned long old_strtoul (const char *, char **, int), new_strtoul (const char *, char **, int);
long long old_strtoll (const char *, char **, int), new_strtoll (const char *, char **, int);
unsigned long long old_strtoull (const char *, char **, int), new_strtoull (const char *, char **, int);

static long checks, fails;

#define TRY(f, T) do { char *e1, *e2; errno = 0; T r1 = old_##f (s, &e1, base); int x1 = errno; \
  errno = 0; T r2 = new_##f (s, &e2, base); int x2 = errno; checks++; \
  if (r1 != r2 || e1 != e2 || x1 != x2) { if (fails++ < 20) printf ("FAIL %s \"%s\" base %d\n", #f, s, base); } } while (0)

int
main (void)
{
  static const char *fixed[] = {
    "0", "-0", "+0", "  42", "\t-42x", "123456", "2147483647", "2147483648",
    "-2147483648", "-2147483649", "4294967295", "4294967296", "-1",
    "9223372036854775807", "9223372036854775808", "-9223372036854775808",
    "-9223372036854775809", "18446744073709551615", "18446744073709551616",
    "99999999999999999999999", "0x7fffffff", "0X80000000", "0xffffffffffffffff",
    "0x", "0xg", "077", "08", "z", "Zz", "", " ", "-", "+-1", "1e5", NULL };
  char s[64];
  unsigned seed = 1;
  for (int base = 0; base <= 36; base++)
    {
      if (base == 1)
	continue;
      for (int i = 0; fixed[i]; i++)
	{
	  strcpy (s, fixed[i]);
	  TRY (strtol, long); TRY (strtoul, unsigned long);
	  TRY (strtoll, long long); TRY (strtoull, unsigned long long);
	}
      for (int k = 0; k < 20000; k++)
	{
	  int len = 0;
	  seed = seed * 1103515245u + 12345u;
	  if (seed & 0x10000) s[len++] = (seed & 0x20000) ? '-' : '+';
	  int nd = 1 + (seed >> 20) % 24;
	  for (int d = 0; d < nd; d++)
	    {
	      seed = seed * 1103515245u + 12345u;
	      s[len++] = "0123456789abcdefghijklmnopqrstuvwxyzABCZ9"[(seed >> 16) % 41];
	    }
	  s[len] = 0;
	  TRY (strtol, long); TRY (strtoul, unsigned long);
	  TRY (strtoll, long long); TRY (strtoull, unsigned long long);
	}
    }
  printf ("strto: %ld checks, %ld failed\n", checks, fails);
  return fails != 0;
}
