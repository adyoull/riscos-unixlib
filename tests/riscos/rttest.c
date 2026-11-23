/* rttest: the time of day (riscos-unixlib T7, from riscos-unixlib-web W39).

   Checks: clock_gettime (CLOCK_REALTIME) and gettimeofday agree with
   RISC OS's own clock (OS_Word 14, read and converted here) to within
   3 cs, at the start and again after about 3 seconds; nanoseconds stay
   under 10^9 and microseconds under 10^6; over 200000 reads it never goes
   back by more than 2 cs (it may, when the offset is put right, by less);
   it advances (several different values in 1 cs). Prints what a read
   costs, and what OS_Word 14 costs, for comparison.  */

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <kernel.h>
#include <swis.h>

static int failures;
#define CHECK(c, ...) do { if (c) printf ("ok    " __VA_ARGS__); \
  else { printf ("FAIL  " __VA_ARGS__); failures++; } printf ("\n"); } while (0)

/* Unix time in cs from OS_Word 14 (5-byte cs since 1900).  */
static int64_t
riscos_cs (void)
{
  unsigned buf[2] = { 3, 0 };
  _kernel_osword (14, (int *) buf);
  int64_t t = ((int64_t) (buf[1] & 0xFF) << 32) | buf[0];
  return t - 0x336E996A00ll;		/* 1900 -> 1970 in cs */
}

static int64_t
rt_ns (void)
{
  struct timespec t;
  clock_gettime (CLOCK_REALTIME, &t);
  return (int64_t) t.tv_sec * 1000000000ll + t.tv_nsec;
}

static unsigned
mono_cs (void)
{
  unsigned c = 0;
  _swix (OS_ReadMonotonicTime, _OUT(0), &c);
  return c;
}

static void
compare (const char *when)
{
  int64_t a = riscos_cs ();
  int64_t r = rt_ns () / 10000000ll;
  int64_t b = riscos_cs ();
  struct timeval tv;
  gettimeofday (&tv, NULL);
  int64_t g = (int64_t) tv.tv_sec * 100 + tv.tv_usec / 10000;
  int64_t c = riscos_cs ();
  CHECK (r >= a - 3 && r <= b + 3, "CLOCK_REALTIME %s: %lld cs, RISC OS %lld..%lld",
	 when, (long long) r, (long long) a, (long long) b);
  CHECK (g >= b - 3 && g <= c + 3, "gettimeofday %s: %lld cs, RISC OS %lld..%lld",
	 when, (long long) g, (long long) b, (long long) c);
  CHECK (tv.tv_usec >= 0 && tv.tv_usec < 1000000, "gettimeofday usec %ld in range", (long) tv.tv_usec);
}

int
main (void)
{
  printf ("rttest: T7 time of day\n");
  compare ("at start");

  int64_t prev = rt_ns (), worst = 0;
  int bad_ns = 0, distinct = 0;
  unsigned c0 = mono_cs (), c1 = c0;
  struct timespec t;
  for (int i = 0; i < 200000; i++)
    {
      clock_gettime (CLOCK_REALTIME, &t);
      if (t.tv_nsec < 0 || t.tv_nsec >= 1000000000) bad_ns++;
      int64_t n = (int64_t) t.tv_sec * 1000000000ll + t.tv_nsec;
      if (prev - n > worst) worst = prev - n;
      if (n != prev && mono_cs () == c0) distinct++;
      prev = n;
    }
  c1 = mono_cs ();
  CHECK (bad_ns == 0, "nanoseconds in range (%d out)", bad_ns);
  CHECK (worst <= 20000000, "never back by more than 2 cs (worst %lld ns)", (long long) worst);
  printf ("info  %d different values within the first centisecond\n", distinct);
  printf ("info  CLOCK_REALTIME read: %.2f us (%u cs for 200000 reads)\n",
	  (c1 - c0) * 10000.0 / 200000, c1 - c0);

  c0 = mono_cs ();
  for (int i = 0; i < 20000; i++) (void) riscos_cs ();
  c1 = mono_cs ();
  printf ("info  OS_Word 14 read: %.2f us\n", (c1 - c0) * 10000.0 / 20000);

  c0 = mono_cs ();
  while (mono_cs () - c0 < 300) (void) rt_ns ();
  compare ("after 3 s");

  printf ("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
  return failures != 0;
}
