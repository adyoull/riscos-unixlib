/* preempttest: threads switched only by the ticker (riscos-unixlib).

   Two threads spin without ever yielding, and the main thread busy-waits
   too, so only the thread ticker's callback (pthread/_context.s) can
   switch between them. Both threads must count past 200000 within 20 s.
   Checks the context switcher's return to a thread, which the RISC OS for
   Linux change (pvops) routes through the port's HAL there; on a Pi it
   must behave as before. From the ROL-Testbed project's preempt.c.  */
#include <pthread.h>
#include <stdio.h>
#include <time.h>

static volatile unsigned long count[2];
static volatile int stop;

static void *
spin (void *arg)
{
  int i = (int) (long) arg;
  double x = 1;

  while (!stop)
    {
      count[i]++;
      x = x * 1.0000001 + 1e-9;		/* some VFP state to keep */
    }
  return (void *) (long) (x > 0);
}

int
main (void)
{
  pthread_t t[2];
  void *r[2];
  clock_t start = clock ();
  long loops = 0;

  for (int i = 0; i < 2; i++)
    pthread_create (&t[i], NULL, spin, (void *) (long) i);
  while ((count[0] < 200000 || count[1] < 200000)
	 && clock () - start < 20 * CLOCKS_PER_SEC)
    loops++;				/* no yield: only the ticker switches */
  stop = 1;
  pthread_join (t[0], &r[0]);
  pthread_join (t[1], &r[1]);
  printf ("counts %lu and %lu after %.1f s (main loop %ld)\n",
	  count[0], count[1], (double) (clock () - start) / CLOCKS_PER_SEC,
	  loops);
  int ok = count[0] >= 200000 && count[1] >= 200000 && r[0] && r[1];
  printf ("%s\n", ok ? "PASS" : "FAIL");
  return !ok;
}
