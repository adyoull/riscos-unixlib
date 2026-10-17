/* itimertest: interval timers (riscos-unixlib).

   1. setitimer (ITIMER_REAL) with a first expiry of 200 ms and an interval
      of 100 ms: 6 SIGALRMs must arrive within 2 s. Before 5.0.3.4 the
      interval's microseconds were taken as centiseconds, so only the first
      alarm came (the next would have been ~17 minutes later).
   2. A 1 s interval: 3 alarms about 1 s apart (this always worked).
   The SIGALRM is raised from a callback through __raise, which the RISC
   OS for Linux change (pvops) touches.

   The handler re-installs itself: UnixLib resets a handler to SIG_DFL when
   it is delivered, even one set with sigaction (a known difference from
   POSIX, left as it is).  */
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

static volatile int alarms;

static void
handler (int sig)
{
  signal (sig, handler);
  alarms++;
}

static int
run (const char *what, long first_us, long every_us, int want, double limit)
{
  struct itimerval it = { { every_us / 1000000, every_us % 1000000 },
			  { first_us / 1000000, first_us % 1000000 } };
  struct itimerval off = { { 0, 0 }, { 0, 0 } };
  clock_t start;
  double t;

  alarms = 0;
  signal (SIGALRM, handler);
  start = clock ();
  setitimer (ITIMER_REAL, &it, NULL);
  while (alarms < want
	 && (t = (double) (clock () - start) / CLOCKS_PER_SEC) < limit)
    ;
  t = (double) (clock () - start) / CLOCKS_PER_SEC;
  setitimer (ITIMER_REAL, &off, NULL);
  printf ("%-40s %d alarms in %.2f s: %s\n", what, alarms, t,
	  alarms >= want ? "ok" : "FAIL");
  return alarms >= want;
}

int
main (void)
{
  int ok = run ("200 ms, then every 100 ms (want 6, <2 s)", 200000, 100000,
		6, 2.0);
  ok &= run ("now, then every 1 s (want 3, <4 s)", 10000, 1000000, 3, 4.0);
  printf ("%s\n", ok ? "PASS" : "FAIL");
  return !ok;
}
