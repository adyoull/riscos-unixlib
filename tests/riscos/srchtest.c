/* srchtest: the string searches of 5.0.3.11 (C8) on the real processor
   give exactly the results of the old versions (copied below from
   5.0.3.10): strstr, strrchr, strspn, strcspn, strpbrk and memmem, for
   every alignment (0-7), haystacks of 0-48 bytes, needles and sets of
   0-6 bytes from a small alphabet (so there are many near matches),
   bytes above 127 and out-of-range strrchr arguments.  Then old and new
   are timed on a 120-byte line of text.  Must print PASS.  */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <time.h>

/* The 5.0.3.10 versions.  */
static char *
old_strrchr (const char *s, int c_)
{
  int i;
  const char *_s;
  char c = (char) c_;

  _s = 0;
  do
    if ((i = *s) == c)
      _s = s;
  while (s++, i);

  return ((char *) _s);
}
static char *
old_strstr (const char *s1, const char *s2)
{
  size_t l1 = strlen (s1), l2 = strlen (s2);
  if (l2 > l1)
    return NULL;
  const char *e1 = s1 + l1 - l2;

  while (s1 <= e1)
    {
      if (!strncmp (s1, s2, l2))
	return (char *) s1;
      s1++;
    }

  return NULL;
}
static size_t
old_strspn (const char *s1, const char *s)
{
  int c1, c2;
  const char *s2;
  size_t n = 0;

  while ((c1 = *s1++))
    {
      s2 = s;
      while ((c2 = *s2++) != c1)
	if (!c2)
	  return n;
      n++;
    }

  return n;
}
static size_t
old_strcspn (const char *s1, const char *s)
{
  int c1, c2;
  const char *s2;
  size_t n = 0;

  while ((c1 = *s1++))
    {
      s2 = s;
      while ((c2 = *s2++))
	if (c2 == c1)
	  return n;
      n++;
    }

  return n;
}
static char *
old_strpbrk (const char *s1, const char *s)
{
  int c1, c2;
  const char *s2;

  while ((c1 = *s1))
    {
      s2 = s;
      while ((c2 = *s2++))
	if (c2 == c1)
	  return (char *) s1;
      s1++;
    }

  return NULL;
}
static void *
old_memmem (const void *l, size_t l_len, const void *s, size_t s_len)
{
  char *cur, *last;
  const char *cl = (const char *) l;
  const char *cs = (const char *) s;

  if (l_len == 0 || s_len == 0)
    return NULL;
  if (l_len < s_len)
    return NULL;
  if (s_len == 1)
    return memchr (l, (int) *cs, l_len);
  last = (char *) cl + l_len - s_len;
  for (cur = (char *) cl; cur <= last; cur++)
    if (cur[0] == cs[0] && memcmp (cur, cs, s_len) == 0)
      return cur;

  return NULL;
}

/* Called through pointers, so the compiler can't use its own versions.  */
static char *(*volatile n_strrchr) (const char *, int) = strrchr;
static char *(*volatile n_strstr) (const char *, const char *) = strstr;
static size_t (*volatile n_strspn) (const char *, const char *) = strspn;
static size_t (*volatile n_strcspn) (const char *, const char *) = strcspn;
static char *(*volatile n_strpbrk) (const char *, const char *) = strpbrk;
static void *(*volatile n_memmem) (const void *, size_t, const void *, size_t) = memmem;

static long checks, fails;
#define CHECK(c, what) do { checks++; if (!(c) && fails++ < 10) \
  printf ("WRONG %s align %d len %d needle %d\n", what, a, len, nl); } while (0)

static unsigned int seed = 12345;
static unsigned int
rnd (void)
{
  seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
  return seed;
}
static unsigned char
pick (void)
{
  static const unsigned char abc[] = { 'a', 'b', 'a', 'b', 'c', 0xE9, 0xFF, ' ' };
  return abc[rnd () % sizeof abc];
}

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
  static char buf[80], nd[16];
  static const int cs[] = { 'a', 0, 0xE9, 0xFF, 'a' + 0x100, -1, -23 };

  for (int rep = 0; rep < 60; rep++)
    for (int a = 0; a < 8; a++)
      for (int len = 0; len <= 48; len++)
	for (int nl = 0; nl <= 6; nl++)
	  {
	    char *h = buf + a;
	    for (int i = 0; i < len; i++)
	      h[i] = (char) pick ();
	    h[len] = 0;
	    for (int i = 0; i < nl; i++)
	      nd[i] = (char) pick ();
	    nd[nl] = 0;
	    if (len >= nl && rep & 1)
	      memcpy (nd, h + rnd () % (len - nl + 1), nl);
	    CHECK (old_strstr (h, nd) == n_strstr (h, nd), "strstr");
	    CHECK (old_strspn (h, nd) == n_strspn (h, nd), "strspn");
	    CHECK (old_strcspn (h, nd) == n_strcspn (h, nd), "strcspn");
	    CHECK (old_strpbrk (h, nd) == n_strpbrk (h, nd), "strpbrk");
	    CHECK (old_memmem (h, len, nd, nl) == n_memmem (h, len, nd, nl), "memmem");
	    if (nl == 0)
	      for (unsigned int i = 0; i < sizeof cs / sizeof cs[0]; i++)
		CHECK (old_strrchr (h, cs[i]) == n_strrchr (h, cs[i]), "strrchr");
	  }
  printf ("%ld checks, %ld wrong\n", checks, fails);

  /* Speed on a 120-byte line of text, 200000 calls each.  */
  static char t[128] __attribute__ ((aligned (8))) =
    "The quick brown fox jumps over the lazy dog; Content-Type: text/html; "
    "charset=utf-8, path=/usr/share/doc/unixlib/README";
  volatile int sink = 0;
#define TIME(name, oldcall, newcall) do { double t0 = secs (); \
  for (int i = 0; i < 200000; i++) sink += (int) (oldcall); double t1 = secs (); \
  for (int i = 0; i < 200000; i++) sink += (int) (newcall); double t2 = secs (); \
  printf ("%-16s old %6.0f ns  new %6.0f ns\n", name, (t1 - t0) * 5e3, (t2 - t1) * 5e3); } while (0)
  TIME ("strstr found", old_strstr (t, "charset="), n_strstr (t, "charset="));
  TIME ("strstr missing", old_strstr (t, "boundary="), n_strstr (t, "boundary="));
  TIME ("strrchr", old_strrchr (t, '/'), n_strrchr (t, '/'));
  TIME ("strcspn", old_strcspn (t, ";,=\r\n"), n_strcspn (t, ";,=\r\n"));
  TIME ("strpbrk", old_strpbrk (t, "=/\r\n"), n_strpbrk (t, "=/\r\n"));
  TIME ("strspn short", old_strspn (t + 44, " \t\r\n"), n_strspn (t + 44, " \t\r\n"));
  TIME ("memmem missing", old_memmem (t, 120, "zzq", 3), n_memmem (t, 120, "zzq", 3));

  printf ("%s\n", fails ? "FAIL" : "PASS");
  return fails != 0;
}
