/* clocktest: the monotonic clock (riscos-unixlib T6, from riscos-unixlib-web W32).

   Checks: clock_gettime (CLOCK_MONOTONIC) never goes backwards over
   200000 reads; nanoseconds stay under 10^9; it has finer than
   centisecond resolution (several different values within one
   centisecond); over about 3 seconds it keeps pace with RISC OS's
   centisecond clock (OS_ReadMonotonicTime) to within 2 cs; nanosleep of
   20 ms takes 20-40 ms by it; natively (T8), each read's centisecond lies
   between OS_ReadMonotonicTime read just before and just after it.
   Prints what a read costs and which way it reads (Linux's clock under
   RISC OS on Linux, else the HAL counter).  */

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <swis.h>

static int failures;
#define CHECK(c, ...) do { if (c) printf ("ok    " __VA_ARGS__); \
  else { printf ("FAIL  " __VA_ARGS__); failures++; } printf ("\n"); } while (0)

static uint64_t
now_ns (void)
{
  struct timespec t;
  clock_gettime (CLOCK_MONOTONIC, &t);
  return (uint64_t) t.tv_sec * 1000000000u + t.tv_nsec;
}

static unsigned
cs (void)
{
  unsigned c = 0;
  _swix (OS_ReadMonotonicTime, _OUT(0), &c);
  return c;
}

int
main (void)
{
  /* (which way: the SWI exists only under RISC OS on Linux; a Linux
     call with a null pointer just fails with -EFAULT) */
  int linux = _swix (0x59EC0, _INR(0,1)|_IN(7), 1, 0, 263) == NULL;
  printf ("RISC OS on Linux (IXSupport_LinuxSyscall): %s\n", linux ? "yes" : "no");

  /* 1. never backwards, nanoseconds in range */
  struct timespec t;
  int bad_nsec = 0, backwards = 0;
  uint64_t last = 0;
  unsigned c0 = cs ();
  for (int i = 0; i < 200000; i++)
    {
      clock_gettime (CLOCK_MONOTONIC, &t);
      if (t.tv_nsec < 0 || t.tv_nsec >= 1000000000)
	bad_nsec++;
      uint64_t n = (uint64_t) t.tv_sec * 1000000000u + t.tv_nsec;
      if (n < last)
	backwards++;
      last = n;
    }
  unsigned c1 = cs ();
  CHECK (backwards == 0, "200000 reads, never backwards (%d)", backwards);
  CHECK (bad_nsec == 0, "nanoseconds always under 10^9 (%d bad)", bad_nsec);
  printf ("      a read costs %.2f us\n", (c1 - c0) * 10000.0 / 200000);

  /* 2. finer than a centisecond */
  unsigned start = cs ();
  while (cs () == start)
    ;
  start = cs ();
  uint64_t seen[64];
  int distinct = 0;
  while (cs () == start && distinct < 64)
    {
      uint64_t n = now_ns ();
      if (!distinct || n != seen[distinct - 1])
	seen[distinct++] = n;
    }
  CHECK (distinct >= 5, "finer than a centisecond (%d different values in one)", distinct);

  /* 3. keeps pace with OS_ReadMonotonicTime */
  start = cs ();
  while (cs () == start)
    ;
  unsigned a = cs ();
  uint64_t na = now_ns ();
  while (cs () - a < 300)
    ;
  unsigned b = cs ();
  uint64_t nb = now_ns ();
  long diff = (long) ((nb - na) / 10000000u) - (long) (b - a);
  CHECK (diff >= -2 && diff <= 2, "over %u cs it counted %lu cs (%+ld)", b - a,
	 (unsigned long) ((nb - na) / 10000000u), diff);

  /* 4. nanosleep by it */
  struct timespec want = { 0, 20000000 };
  uint64_t s0 = now_ns ();
  nanosleep (&want, NULL);
  uint64_t slept = now_ns () - s0;
  CHECK (slept >= 20000000 && slept <= 40000000, "nanosleep 20 ms took %.1f ms", slept / 1e6);

  /* 5. (T8) each read agrees with OS_ReadMonotonicTime read just before
     and just after it: its centisecond is between the two.  A pair of
     readings that didn't match (the counter's reload missed) would give
     a time a centisecond early.  Native only: under RISC OS on Linux the
     clock is Linux's, with another starting point.  A sanity check
     rather than a stress test: an idle machine rarely puts 625 us
     between two SWIs.  */
  if (!linux)
    {
      int early = 0, late = 0;
      for (int i = 0; i < 100000; i++)
	{
	  unsigned before = cs ();
	  uint64_t n = now_ns ();
	  unsigned after = cs ();
	  unsigned tcs = (unsigned) (n / 10000000u);
	  if (tcs < before)
	    early++;
	  else if (tcs > after)
	    late++;
	}
      CHECK (early == 0 && late == 0,
	     "100000 reads between OS_ReadMonotonicTime's (%d early, %d late)",
	     early, late);
    }


  printf ("%s: %d failure%s\n", failures ? "FAILED" : "PASSED", failures,
	  failures == 1 ? "" : "s");
  return failures != 0;
}
