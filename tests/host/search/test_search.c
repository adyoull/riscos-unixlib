/* C8: compare the new strrchr, strstr, strspn, strcspn, strpbrk and memmem
   with the old ones on random input, at every alignment, and with the
   haystack ending at a page with no access after it.  C9: strncpy, the
   whole destination compared (padding, and nothing written past N).  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define DECL(v) \
  char *v##_strrchr (const char *, int); \
  char *v##_strstr (const char *, const char *); \
  size_t v##_strspn (const char *, const char *); \
  size_t v##_strcspn (const char *, const char *); \
  char *v##_strpbrk (const char *, const char *); \
  void *v##_memmem (const void *, size_t, const void *, size_t); \
  char *v##_strncpy (char *, const char *, size_t);
DECL (old)
DECL (new)

static unsigned long checks, fails;
#define CHECK(what, a, b) do { checks++; if ((a) != (b)) { \
  if (fails++ < 20) printf ("FAIL %s: case %lu\n", what, checks); } } while (0)

static unsigned rnd (void) { static unsigned x = 12345; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; }

/* A byte from a small alphabet, sometimes a high byte.  */
static unsigned char pick (int alpha)
{
  unsigned r = rnd () % 16;
  if (r == 0) return 0x80 + rnd () % 128;
  return 'a' + rnd () % alpha;
}

static void
one (const char *h, size_t hl, const char *n, size_t nl)
{
  (void) hl;
  CHECK ("strstr", old_strstr (h, n), new_strstr (h, n));
  CHECK ("strspn", old_strspn (h, n), new_strspn (h, n));
  CHECK ("strcspn", old_strcspn (h, n), new_strcspn (h, n));
  CHECK ("strpbrk", old_strpbrk (h, n), new_strpbrk (h, n));
  CHECK ("memmem", old_memmem (h, hl, n, nl), new_memmem (h, hl, n, nl));
  int cs[] = { n[0], 0, pick (4), 0xff, 'a' + 0x100, -1, -0x9f, 0x7fffffff };
  for (int i = 0; i < (int) (sizeof (cs) / sizeof (cs[0])); i++)
    CHECK ("strrchr", old_strrchr (h, cs[i]), new_strrchr (h, cs[i]));
}

int
main (void)
{
  long pg = sysconf (_SC_PAGESIZE);
  char *map = mmap (NULL, 2 * pg, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  mprotect (map + pg, pg, PROT_NONE);
  static char buf[256 + 8], nd[64];

  for (int iter = 0; iter < 400000; iter++)
    {
      int alpha = 1 + rnd () % 6;
      size_t hl = rnd () % 200, nl = rnd () % 8 ? rnd () % 7 : rnd () % 48;
      size_t off = rnd () % 8;
      char *h = buf + off;
      for (size_t i = 0; i < hl; i++) h[i] = pick (alpha);
      h[hl] = 0;
      for (size_t i = 0; i < nl; i++) nd[i] = pick (alpha);
      nd[nl] = 0;
      /* Sometimes the needle is taken from the haystack.  */
      if (hl > nl && rnd () % 3 == 0)
	memcpy (nd, h + rnd () % (hl - nl + 1), nl);
      one (h, hl, nd, nl);
      /* The same haystack ending at the last byte before the guard page.  */
      char *e = map + pg - hl - 1;
      memcpy (e, h, hl + 1);
      one (e, hl, nd, nl);
    }
  /* C9: strncpy.  Source and destination at every pair of alignments,
     strings of 0-40 bytes, N of 0-48, and the source ending at the
     guard page (so a read past what strncpy may read faults).  */
  {
    static unsigned char d1[128], d2[128];
    for (int sa = 0; sa < 8; sa++)
      for (int da = 0; da < 8; da++)
	for (size_t len = 0; len <= 40; len++)
	  for (size_t n = 0; n <= 48; n++)
	    {
	      char *src = buf + sa;
	      for (size_t i = 0; i < len; i++)
		src[i] = (char) (1 + rnd () % 255);
	      src[len] = 0;
	      memset (d1, 0xEE, sizeof d1);
	      memset (d2, 0xEE, sizeof d2);
	      char *r1 = old_strncpy ((char *) d1 + da, src, n);
	      char *r2 = new_strncpy ((char *) d2 + da, src, n);
	      CHECK ("strncpy result", r1 - (char *) d1, r2 - (char *) d2);
	      checks++;
	      if (memcmp (d1, d2, sizeof d1) && fails++ < 20)
		printf ("FAIL strncpy: src +%d dst +%d len %zu n %zu\n", sa, da, len, n);
	      /* Unterminated: the N bytes end at the guard page.  */
	      if (n > 0 && n <= 48)
		{
		  char *e = map + pg - n;
		  memset (e, 'x', n);
		  memset (d2, 0xEE, sizeof d2);
		  new_strncpy ((char *) d2 + da, e, n);
		  checks++;
		  if ((memcmp (d2 + da, e, n) || d2[da + n] != 0xEE) && fails++ < 20)
		    printf ("FAIL strncpy unterminated: dst +%d n %zu\n", da, n);
		}
	    }
  }
  printf ("search: %lu checks, %lu failures\n", checks, fails);
  return fails != 0;
}
