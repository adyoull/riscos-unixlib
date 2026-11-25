/* Host test for C5 (string/strcmp.c, strncmp.c, memcmp.c, strchr.c,
   memchr.c, stricmp.c, strnicmp.c): the new word-at-a-time and fast-path
   versions (new_*) give exactly the results of the old ones (old_*, from
   git), for every alignment of both arguments (0-7), lengths 0-40, every
   position of a difference or a terminator, and bytes above 127; and they
   never read past the end of a string or block that ends at a page
   boundary followed by an inaccessible page.  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define DECL(p) \
  int p##strcmp (const char *, const char *); \
  int p##strncmp (const char *, const char *, size_t); \
  int p##memcmp (const void *, const void *, size_t); \
  char *p##strchr (const char *, int); \
  void *p##memchr (const void *, int, size_t); \
  int p##stricmp (const char *, const char *); \
  int p##strnicmp (const char *, const char *, size_t);
DECL (old_)
DECL (new_)

static long checks, fails;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf ("FAIL "); printf (__VA_ARGS__); printf ("\n"); } } } while (0)

static const unsigned char vals[] = { 'a', 'A', 'z', 'Z', '0', 0x80, 0xE9, 0xFF, 0x01 };

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
	      if (v == 1 && diff >= 0 && s1[diff] >= 'a' && s1[diff] <= 'z')
		s2[diff] = s1[diff] - 32;		/* a case difference */
	      s1[len] = 0;
	      s2[len] = (v & 1) ? 0 : 'x';		/* s2 longer, or equal */
	      const char *c1 = (const char *) s1, *c2 = (const char *) s2;
	      CHECK (old_strcmp (c1, c2) == new_strcmp (c1, c2), "strcmp a%d a%d len %d diff %d v %u", a1, a2, len, diff, v);
	      CHECK (old_stricmp (c1, c2) == new_stricmp (c1, c2), "stricmp a%d a%d len %d diff %d v %u", a1, a2, len, diff, v);
	      for (int n = 0; n <= len + 2; n += (n < 6 ? 1 : 5))
		{
		  CHECK (old_strncmp (c1, c2, n) == new_strncmp (c1, c2, n), "strncmp n %d a%d a%d len %d diff %d", n, a1, a2, len, diff);
		  CHECK (old_strnicmp (c1, c2, n) == new_strnicmp (c1, c2, n), "strnicmp n %d a%d a%d len %d diff %d", n, a1, a2, len, diff);
		  CHECK (old_memcmp (s1, s2, n) == new_memcmp (s1, s2, n), "memcmp n %d a%d a%d len %d diff %d", n, a1, a2, len, diff);
		}
	      if (a2 == 0 && v < 4)
		{
		  int ch = diff >= 0 ? s1[diff] : (v == 0 ? 0 : vals[v]);
		  CHECK (old_strchr (c1, ch) == new_strchr (c1, ch), "strchr a%d len %d c %d", a1, len, ch);
		  CHECK (old_strchr (c1, ch + 256) == new_strchr (c1, ch + 256), "strchr c+256");
		  for (int n = 0; n <= len + 1; n++)
		    CHECK (old_memchr (s1, ch, n) == new_memchr (s1, ch, n), "memchr a%d len %d n %d c %d", a1, len, n, ch);
		}
	    }

  /* Nothing read past a page that ends the data.  */
  long pg = sysconf (_SC_PAGESIZE);
  unsigned char *m = mmap (NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  unsigned char *m2 = mmap (NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  mprotect (m + pg, pg, PROT_NONE);
  mprotect (m2 + pg, pg, PROT_NONE);
  for (int len = 0; len < 20; len++)
    {
      unsigned char *e1 = m + pg, *e2 = m2 + pg;
      memset (e1 - 64, 'k', 64);
      memset (e2 - 64, 'k', 64);
      e1[-1] = 0;
      e2[-1] = 0;
      const char *c1 = (const char *) (e1 - 1 - len), *c2 = (const char *) (e2 - 1 - len);
      CHECK (new_strcmp (c1, c2) == 0, "strcmp at page end %d", len);
      CHECK (new_stricmp (c1, c2) == 0, "stricmp at page end");
      CHECK (new_strncmp (c1, c2, 1000) == 0, "strncmp at page end");
      CHECK (new_strnicmp (c1, c2, 1000) == 0, "strnicmp at page end");
      CHECK (new_strchr (c1, 'z') == NULL, "strchr at page end");
      CHECK (new_strchr (c1, 0) == (char *) e1 - 1, "strchr 0 at page end");
      CHECK (new_memcmp (e1 - len, e2 - len, len) == 0, "memcmp at page end");
      CHECK (new_memchr (e1 - len, 'z', len) == NULL, "memchr at page end");
      /* A match in the last word(s) of the page and an N far larger than
	 the object (POSIX lets memchr stop at the match).  */
      e1[-1 - (len % 8)] = 'z';
      CHECK (new_memchr (e1 - 8 - (len % 8), 'z', 1u << 20) == e1 - 1 - (len % 8),
	     "memchr with a large n at page end");
      CHECK (new_memchr (e1 - 8 - (len % 8), 'z', (size_t) -1) == e1 - 1 - (len % 8),
	     "memchr with n = SIZE_MAX at page end");
      e1[-1 - (len % 8)] = 'k';
      e1[-4 + (len % 4)] = 'z';	/* in the page's last word, read from its start */
      CHECK (new_memchr (e1 - 4, 'z', 1u << 20) == e1 - 4 + (len % 4),
	     "memchr: match in the last word, large n");
      e1[-4 + (len % 4)] = 'k';
      /* s1 longer than s2 at s2's page end, and a difference in the
	 last word.  */
      memset (e1 - 64, 'k', 64);
      e1[-1] = 0;
      CHECK (new_strcmp (c1 - 4, c2) != 0 || len < 0, "strcmp longer s1");
      e2[-2] = 'j';
      if (len >= 1)
	CHECK ((new_strcmp (c1, c2) > 0) == (strcmp (c1, c2) > 0), "strcmp last-word difference");
      e2[-2] = 'k';
    }

  printf ("strings: %ld checks, %ld failed\n", checks, fails);
  return fails != 0;
}
