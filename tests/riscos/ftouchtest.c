/* ftouchtest: memcpy, memmove, memset, mempcpy and bcopy into and out of
   stack pages that have never been touched (M3), in a new thread whose
   stack ARMEABISupport maps a page at a time, and into new anonymous
   mmap memory (mapped when allocated: a check that nothing changed
   there); setjmp with its jmp_buf running into a fresh page; and
   bcopy's direction (B2).  Before 5.0.3.11-rc8 the program could die here
   (an abort in SVC mode, or "Unixlib detected that ARMEABISupport could
   not handle an abort", EMT trap): memcpy's 64-byte VSTM, or setjmp's,
   was the first touch of a page, and ARMEABISupport 1.07+ can complete
   at most 60 bytes.  Must print PASS.  */
#define _GNU_SOURCE
#include <pthread.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static void *(*volatile p_memcpy) (void *, const void *, size_t) = memcpy;
static void *(*volatile p_memmove) (void *, const void *, size_t) = memmove;
static void *(*volatile p_memset) (void *, int, size_t) = memset;
static void *(*volatile p_mempcpy) (void *, const void *, size_t) = mempcpy;
static void (*volatile p_bcopy) (const void *, void *, size_t) = bcopy;

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c) && fails++ < 10) { printf ("WRONG: " __VA_ARGS__); putchar ('\n'); } } while (0)

static unsigned char src[20000];

static void *
run (void *arg)
{
  (void) arg;
  char here;
  /* Fresh pages: 64 KB below this frame and further down, a new page (or
     two) for each case, at a few offsets in the page.  */
  unsigned base = ((unsigned) &here - 64 * 1024) & ~0xFFFu;
  static const unsigned sizes[] = { 1, 3, 16, 64, 100, 300, 4096, 5000, 9000 };
  static const unsigned offs[] = { 0, 4, 0xFF0, 0xFFD };
  int k = 0;

  for (unsigned f = 0; f < 5; f++)
    for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++)
      for (unsigned o = 0; o < 4; o++)
	{
	  unsigned n = sizes[s];
	  /* Each case in a fresh 16 KB stretch below the last (3 MB in all).  */
	  base -= 16 * 1024;
	  unsigned char *p = (unsigned char *) (base + offs[o]);
	  const char *name = "";
	  switch (f)
	    {
	    case 0: name = "memcpy"; p_memcpy (p, src + k % 7, n); break;
	    case 1: name = "memmove"; p_memmove (p, src + k % 7, n); break;
	    case 2: name = "memset"; p_memset (p, 0x3C, n); break;
	    case 3: name = "mempcpy"; CHECK (p_mempcpy (p, src + k % 7, n) == p + n, "mempcpy's result"); break;
	    case 4: name = "bcopy"; p_bcopy (src + k % 7, p, n); break;
	    }
	  int ok = 1;
	  if (f == 2)
	    {
	      for (unsigned i = 0; i < n; i++)
		ok &= p[i] == 0x3C;
	    }
	  else
	    ok = !memcmp (p, src + k % 7, n);
	  CHECK (ok, "%s of %u bytes into a fresh page (+%x)", name, n, offs[o]);
	  k++;
	}
  /* setjmp: its 64 bytes of d8-d15 running into a fresh page.  */
  for (unsigned slack = 8; slack <= 64; slack += 8)
    {
      base -= 16 * 1024;
      jmp_buf *jb = (jmp_buf *) (base + 0x1000 - slack);
      ((volatile unsigned char *) jb)[0] = 0;	/* its first page in use */
      CHECK (setjmp (*jb) == 0, "setjmp %u bytes before a fresh page", slack);
    }
  /* Reading from fresh pages.  Their contents are whatever the page
     held before (RISC OS doesn't clear pages it hands out), so the copy
     is compared with the pages themselves.  */
  for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++)
    {
      static unsigned char dst[9000];
      base -= 16 * 1024;
      p_memcpy (dst, (unsigned char *) base + 8, sizes[s]);
      CHECK (!memcmp (dst, (unsigned char *) base + 8, sizes[s]), "memcpy of %u bytes from a fresh page", sizes[s]);
    }
  return NULL;
}

int
main (void)
{
  for (unsigned i = 0; i < sizeof src; i++)
    src[i] = (unsigned char) (i * 7 + 1);

  /* B2: bcopy (src, dest, n), overlapping both ways.  */
  static unsigned char b[64], want[64];
  for (int shift = -3; shift <= 3; shift++)
    {
      for (int i = 0; i < 64; i++)
	b[i] = want[i] = (unsigned char) i;
      memmove (want + 20 + shift, want + 20, 20);
      p_bcopy (b + 20, b + 20 + shift, 20);
      CHECK (!memcmp (b, want, 64), "bcopy with a shift of %d", shift);
    }

  /* New anonymous mmap memory (ARMEABISupport maps it at mmap time).  */
  unsigned char *m = mmap (NULL, 256 * 1024, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANON, -1, 0);
  CHECK (m != MAP_FAILED, "mmap");
  if (m != MAP_FAILED)
    {
      static const unsigned msizes[] = { 64, 100, 4096, 9000 };
      unsigned char *q = m;
      for (unsigned i = 0; i < 4; i++, q += 16 * 1024)
	{
	  p_memcpy (q + 0x100, src, msizes[i]);
	  CHECK (!memcmp (q + 0x100, src, msizes[i]), "memcpy of %u bytes into fresh mmap pages", msizes[i]);
	}
      static unsigned char dst[9000];
      for (unsigned i = 0; i < 4; i++, q += 16 * 1024)
	{
	  p_memcpy (dst, q + 0x100, msizes[i]);
	  CHECK (!memcmp (dst, q + 0x100, msizes[i]), "memcpy of %u bytes from fresh mmap pages", msizes[i]);
	}
      munmap (m, 256 * 1024);
    }

  pthread_attr_t a;
  pthread_t t;
  pthread_attr_init (&a);
  pthread_attr_setstacksize (&a, 4 * 1024 * 1024);
  pthread_create (&t, &a, run, NULL);
  pthread_join (t, NULL);
  printf ("%d checks, %d wrong\n%s\n", checks, fails, fails ? "FAIL" : "PASS");
  return fails != 0;
}
