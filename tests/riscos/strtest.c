/* strtest: the string compares and searches of 5.0.3.11 (C5) on the real
   processor give exactly the results of the old byte-by-byte versions
   (copied below from 5.0.3.10), for every alignment of both arguments
   (0-7), lengths 0-40, every position of a difference or terminator,
   bytes above 127 and a case difference; and they are timed against the
   old ones on 64-byte strings.  Must print PASS.  */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* The 5.0.3.10 versions.  */
static int
old_strcmp (const char *s1, const char *s2)
{
  int i;

  while ((i = *s1) && i == *s2)
    s1++, s2++;

  return *(const unsigned char *) s1 - *(const unsigned char *) s2;
}
static int
old_strncmp (const char *s1, const char *s2, size_t n)
{
  int i;

  if (!n)
    return 0;

  while ((i = *s1) && i == *s2 && --n)
    s1++, s2++;

  return *(const unsigned char *) s1 - *(const unsigned char *) s2;
}
static int
old_memcmp (const void *s1, const void *s2, size_t n)
{
  const unsigned char *_s1 = s1;
  const unsigned char *_s2 = s2;

  while (n & 0x07)
    {
      if (*_s1 != *_s2)
	goto differs;
      _s1++, _s2++;
      n--;
    }
  n >>= 3;
  while (n)
    {
      if (*_s1 != *_s2)
	goto differs;
      _s1++, _s2++;
      if (*_s1 != *_s2)
	goto differs;
      _s1++, _s2++;
      if (*_s1 != *_s2)
	goto differs;
      _s1++, _s2++;
      if (*_s1 != *_s2)
	goto differs;
      _s1++, _s2++;
      if (*_s1 != *_s2)
	goto differs;
      _s1++, _s2++;
      if (*_s1 != *_s2)
	goto differs;
      _s1++, _s2++;
      if (*_s1 != *_s2)
	goto differs;
      _s1++, _s2++;
      if (*_s1 != *_s2)
	goto differs;
      _s1++, _s2++;
      n--;
    }

  if (n == 0)
    return 0;

differs:
  return *_s1 - *_s2;
}
static char *
old_strchr (const char *s, int c_)
{
  int i;
  char c = (char) c_;

  while ((i = *s) && (i != c))
    s++;

  if (i == c)
    return (char *) s;

  return NULL;
}
static void *
old_memchr (const void *s, int c_, size_t n)
{
  unsigned char *_s = (unsigned char *) s;
  char c = (char) c_;

  while (n & 0x07)
    {
      n--;
      if (*_s++ == c)
	{
ret:
	  return (void *) (--_s);
	}
    }
  n >>= 3;
  while (n)
    {
      n--;
      if (*_s++ == c)
	goto ret;
      if (*_s++ == c)
	goto ret;
      if (*_s++ == c)
	goto ret;
      if (*_s++ == c)
	goto ret;
      if (*_s++ == c)
	goto ret;
      if (*_s++ == c)
	goto ret;
      if (*_s++ == c)
	goto ret;
      if (*_s++ == c)
	goto ret;
    }

  return NULL;
}
static int
old_stricmp (const char *s1, const char *s2)
{
  const unsigned char *p1 = (const unsigned char *) s1;
  const unsigned char *p2 = (const unsigned char *) s2;
  int result = 0;

  if (p1 == p2)
    return result;

  while (! result)
    {
      result = tolower (*p1) - tolower (*p2);
      if (*p1++ == '\0')
	break;
      p2 ++;
    }

  return result;
}
static int
old_strnicmp (const char *s1, const char *s2, size_t n)
{
  int i, j;

  if (!n)
    return 0;

  do
    {
      i = *s1++, j = *s2++;
      i = tolower (i);
      j = tolower (j);
    }
  while (i && i == j && --n);

  return i - j;
}

/* Called through pointers, so the compiler can't use its own versions.  */
static int (*volatile n_strcmp) (const char *, const char *) = strcmp;
static int (*volatile n_strncmp) (const char *, const char *, size_t) = strncmp;
static int (*volatile n_memcmp) (const void *, const void *, size_t) = memcmp;
static char *(*volatile n_strchr) (const char *, int) = strchr;
static void *(*volatile n_memchr) (const void *, int, size_t) = memchr;
static int (*volatile n_strcasecmp) (const char *, const char *) = strcasecmp;
static int (*volatile n_strncasecmp) (const char *, const char *, size_t) = strncasecmp;

static long checks, fails;
#define CHECK(c, what) do { checks++; if (!(c) && fails++ < 10) printf ("WRONG %s a%d a%d len %d diff %d\n", what, a1, a2, len, diff); } while (0)

static const unsigned char vals[] = { 'a', 'A', 'z', 0x80, 0xE9, 0xFF, 0x01 };

static double
secs (void)
{
  struct timespec t;
  clock_gettime (CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}

int
main (void)
{
  static unsigned char b1[128], b2[128];
  for (int a1 = 0; a1 < 8; a1++)
    for (int a2 = 0; a2 < 8; a2++)
      for (int len = 0; len <= 40; len++)
	for (int diff = -1; diff < len; diff++)
	  for (unsigned v = 0; v < sizeof vals; v++)
	    {
	      unsigned char *s1 = b1 + a1, *s2 = b2 + a2;
	      memset (b1, 0x55, sizeof b1);
	      memset (b2, 0x55, sizeof b2);
	      for (int i = 0; i < len; i++)
		s1[i] = s2[i] = (unsigned char) ('a' + (i * 7) % 26);
	      if (diff >= 0)
		s2[diff] = vals[v] == s1[diff] ? 'Q' : vals[v];
	      if (v == 1 && diff >= 0)
		s2[diff] = toupper (s1[diff]);
	      s1[len] = 0;
	      s2[len] = (v & 1) ? 0 : 'x';
	      const char *c1 = (const char *) s1, *c2 = (const char *) s2;
	      CHECK (old_strcmp (c1, c2) == n_strcmp (c1, c2), "strcmp");
	      CHECK (old_stricmp (c1, c2) == n_strcasecmp (c1, c2), "strcasecmp");
	      for (int n = 0; n <= len + 2; n += (n < 6 ? 1 : 7))
		{
		  CHECK (old_strncmp (c1, c2, n) == n_strncmp (c1, c2, n), "strncmp");
		  CHECK (old_strnicmp (c1, c2, n) == n_strncasecmp (c1, c2, n), "strncasecmp");
		  CHECK (old_memcmp (s1, s2, n) == n_memcmp (s1, s2, n), "memcmp");
		}
	      if (a2 == 0 && v < 3)
		{
		  int ch = diff >= 0 ? s1[diff] : (v == 0 ? 0 : vals[v]);
		  CHECK (old_strchr (c1, ch) == n_strchr (c1, ch), "strchr");
		  for (int n = 0; n <= len + 1; n += 3)
		    CHECK (old_memchr (s1, ch, n) == n_memchr (s1, ch, n), "memchr");
		}
	    }
  printf ("%ld checks, %ld wrong\n", checks, fails);

  /* Speed on 64 equal bytes (aligned), 200000 calls each.  */
  static char x[72] __attribute__ ((aligned (8))), y[72] __attribute__ ((aligned (8)));
  memset (x, 'k', 64); memset (y, 'k', 64); x[64] = y[64] = 0;
  volatile int sink = 0;
#define TIME(name, oldcall, newcall) do { double t0 = secs (); \
  for (int i = 0; i < 200000; i++) sink += oldcall; double t1 = secs (); \
  for (int i = 0; i < 200000; i++) sink += newcall; double t2 = secs (); \
  printf ("%-11s old %6.0f ns  new %6.0f ns\n", name, (t1 - t0) * 5e3, (t2 - t1) * 5e3); } while (0)
  TIME ("strcmp", old_strcmp (x, y), n_strcmp (x, y));
  TIME ("strncmp", old_strncmp (x, y, 64), n_strncmp (x, y, 64));
  TIME ("memcmp", old_memcmp (x, y, 64), n_memcmp (x, y, 64));
  TIME ("strcasecmp", old_stricmp (x, y), n_strcasecmp (x, y));
  TIME ("strchr", (int) old_strchr (x, 'z'), (int) n_strchr (x, 'z'));
  TIME ("memchr", (int) old_memchr (x, 'z', 64), (int) n_memchr (x, 'z', 64));

  printf ("%s\n", fails ? "FAIL" : "PASS");
  return fails != 0;
}
