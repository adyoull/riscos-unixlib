/* twtest: timed waits and semaphores (riscos-unixlib, 5.0.3.4-rc1; Rob
   Heaton's riscos-gcc16 reports 11 and 24).

   1. pthread_cond_timedwait with a deadline 300 ms ahead, on CLOCK_REALTIME
      and on CLOCK_MONOTONIC: it must time out after 300 ms (the old code
      overran by up to a second on CLOCK_REALTIME, and on CLOCK_MONOTONIC
      added the clock to an absolute time).
   2. A deadline that has passed: ETIMEDOUT at once.
   3. A signal before the deadline wakes the waiter (return 0).
   4. Semaphores: sem_wait blocks until another thread posts; sem_timedwait
      times out after 200 ms (it was ENOSYS), and returns 0 when posted;
      20000 posts and waits between two threads.
   5. Before any thread is started: sem_timedwait still times out (the
      condition variable wait returns at once then, and sem_timedwait loops
      to its deadline).  A dummy thread is then started, so that the tests
      above go through the scheduler.
   6. A thread blocked in sem_wait is cancelled by pthread_cancel (sem_wait
      is a cancellation point): pthread_join returns PTHREAD_CANCELED.
   Must print PASS.  */
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t c;
static sem_t s, ping, pong;
static int bad;

static void deadline (clockid_t clk, long ms, struct timespec *ts)
{
  clock_gettime (clk, ts);
  ts->tv_sec += ms / 1000;
  ts->tv_nsec += (ms % 1000) * 1000000L;
  if (ts->tv_nsec >= 1000000000L)
    {
      ts->tv_sec++;
      ts->tv_nsec -= 1000000000L;
    }
}

static void check (const char *what, int ok)
{
  printf ("%-60s %s\n", what, ok ? "ok" : "FAIL");
  bad |= !ok;
}

static void timed (clockid_t clk, const char *name)
{
  pthread_condattr_t a;
  struct timespec ts;
  char what[80];

  pthread_condattr_init (&a);
  pthread_condattr_setclock (&a, clk);
  pthread_cond_init (&c, &a);
  pthread_mutex_lock (&m);
  deadline (clk, 300, &ts);
  clock_t t0 = clock ();
  int r = pthread_cond_timedwait (&c, &m, &ts);
  clock_t cs = clock () - t0;
  pthread_mutex_unlock (&m);
  snprintf (what, sizeof what, "%s: 300 ms wait timed out after %ld cs", name,
	    (long) cs);
  check (what, r == ETIMEDOUT && cs >= 29 && cs <= 40);
}

static void *signaller (void *arg)
{
  (void) arg;
  usleep (100000);
  pthread_mutex_lock (&m);
  pthread_cond_signal (&c);
  pthread_mutex_unlock (&m);
  return NULL;
}

static void *poster (void *arg)
{
  (void) arg;
  usleep (200000);
  sem_post (&s);
  return NULL;
}

static void *ponger (void *arg)
{
  (void) arg;
  for (int i = 0; i < 10000; i++)
    {
      sem_wait (&ping);
      sem_post (&pong);
    }
  return NULL;
}

static void *nothing (void *arg)
{
  return arg;
}

static void *blocked (void *arg)
{
  sem_wait ((sem_t *) arg);
  return NULL;			/* not reached: cancelled */
}

int main (void)
{
  struct timespec ts;
  pthread_t t;
  void *ret;

  sem_init (&s, 0, 0);
  deadline (CLOCK_REALTIME, 100, &ts);
  clock_t t1 = clock ();
  errno = 0;
  int r1 = sem_timedwait (&s, &ts);
  check ("no threads yet: sem_timedwait times out after 100 ms",
	 r1 == -1 && errno == ETIMEDOUT && clock () - t1 >= 9);
  sem_destroy (&s);

  pthread_create (&t, NULL, nothing, NULL);
  pthread_join (t, NULL);

  timed (CLOCK_REALTIME, "CLOCK_REALTIME");
  timed (CLOCK_MONOTONIC, "CLOCK_MONOTONIC");

  pthread_cond_init (&c, NULL);
  pthread_mutex_lock (&m);
  deadline (CLOCK_REALTIME, -1000, &ts);
  check ("a deadline that has passed: ETIMEDOUT at once",
	 pthread_cond_timedwait (&c, &m, &ts) == ETIMEDOUT);
  pthread_create (&t, NULL, signaller, NULL);
  deadline (CLOCK_REALTIME, 2000, &ts);
  check ("signalled after 100 ms, deadline 2 s: woken (0)",
	 pthread_cond_timedwait (&c, &m, &ts) == 0);
  pthread_mutex_unlock (&m);
  pthread_join (t, NULL);

  sem_init (&s, 0, 0);
  pthread_create (&t, NULL, poster, NULL);
  clock_t t0 = clock ();
  check ("sem_wait returns when another thread posts",
	 sem_wait (&s) == 0 && clock () - t0 >= 15);
  pthread_join (t, NULL);
  deadline (CLOCK_REALTIME, 200, &ts);
  t0 = clock ();
  errno = 0;
  int r = sem_timedwait (&s, &ts);
  check ("sem_timedwait times out after 200 ms (ETIMEDOUT)",
	 r == -1 && errno == ETIMEDOUT && clock () - t0 >= 19);
  pthread_create (&t, NULL, poster, NULL);
  deadline (CLOCK_REALTIME, 3000, &ts);
  check ("sem_timedwait returns 0 when posted",
	 sem_timedwait (&s, &ts) == 0);
  pthread_join (t, NULL);

  sem_init (&ping, 0, 0);
  sem_init (&pong, 0, 0);
  pthread_create (&t, NULL, ponger, NULL);
  t0 = clock ();
  for (int i = 0; i < 10000; i++)
    {
      sem_post (&ping);
      sem_wait (&pong);
    }
  pthread_join (t, NULL);
  printf ("20000 posts and waits between two threads in %ld cs\n",
	  (long) (clock () - t0));
  sem_init (&s, 0, 0);
  pthread_create (&t, NULL, blocked, &s);
  usleep (100000);
  pthread_cancel (t);
  t0 = clock ();
  ret = NULL;
  pthread_join (t, &ret);
  check ("a thread blocked in sem_wait is cancelled",
	 ret == PTHREAD_CANCELED && clock () - t0 < 100);
  check ("semaphores destroyed", sem_destroy (&s) == 0
	 && sem_destroy (&ping) == 0 && sem_destroy (&pong) == 0);

  printf ("%s\n", bad ? "FAIL" : "PASS");
  return bad;
}
