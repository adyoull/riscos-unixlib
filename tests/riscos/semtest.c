/* semtest: semaphores (riscos-unixlib P7, from riscos-mesa). sem_wait
   blocks instead of polling, sem_timedwait works (it was ENOSYS), and a
   sem_post from a signal handler doesn't stop the program.  */
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int bad;

static void
result (const char *what, int ok, const char *fmt, double v)
{
  printf ("%-44s ", what);
  printf (fmt, v);
  printf (": %s\n", ok ? "ok" : "FAIL");
  bad |= !ok;
}

static double
ms_now (void)
{
  struct timespec t;
  clock_gettime (CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static struct timespec
deadline_ms (long ms)
{
  struct timespec t;
  clock_gettime (CLOCK_REALTIME, &t);
  long long ns = (long long) t.tv_sec * 1000000000LL + t.tv_nsec
		 + (long long) ms * 1000000LL;
  t.tv_sec = ns / 1000000000LL;
  t.tv_nsec = ns % 1000000000LL;
  return t;
}

static sem_t s1, s2;
static volatile int stop;
static volatile unsigned long spins;

static void *
spinner (void *arg)
{
  (void) arg;
  while (!stop)
    spins++;
  return NULL;
}

static void *
post_after_100ms (void *arg)
{
  (void) arg;
  usleep (100000);
  sem_post (&s1);
  return NULL;
}

static void *
wait_s2 (void *arg)
{
  (void) arg;
  sem_wait (&s2);
  return NULL;
}

static void *
pingpong (void *arg)
{
  long n = (long) arg;
  for (long i = 0; i < n; i++)
    {
      sem_wait (&s1);
      sem_post (&s2);
    }
  return NULL;
}

static void
on_alarm (int sig)
{
  (void) sig;
  sem_post (&s1);
}

/* Count main-thread loop iterations for MS milliseconds.  */
static unsigned long
count_for (double ms)
{
  unsigned long n = 0;
  double end = ms_now () + ms;
  while (ms_now () < end)
    n++;
  return n;
}

int
main (void)
{
  pthread_t t, u;
  struct timespec d;
  double t0, el;
  int r;

  sem_init (&s1, 0, 0);
  sem_init (&s2, 0, 0);

  /* 1. Timeout, while another thread keeps running.  */
  stop = 0;
  spins = 0;
  pthread_create (&t, NULL, spinner, NULL);
  d = deadline_ms (200);
  t0 = ms_now ();
  r = sem_timedwait (&s1, &d);
  el = ms_now () - t0;
  int e = errno;
  stop = 1;
  pthread_join (t, NULL);
  result ("timeout 200 ms: ETIMEDOUT after (ms)",
	  r == -1 && e == ETIMEDOUT && el >= 185 && el <= 250, "%.0f", el);
  result ("  another thread ran meanwhile (count)", spins > 1000, "%.0f",
	  (double) spins);

  /* 2. Wake-up by a post after 100 ms, deadline 2 s.  */
  pthread_create (&t, NULL, post_after_100ms, NULL);
  d = deadline_ms (2000);
  t0 = ms_now ();
  r = sem_timedwait (&s1, &d);
  el = ms_now () - t0;
  pthread_join (t, NULL);
  result ("wake-up: 0 after (ms)", r == 0 && el >= 90 && el <= 200, "%.0f", el);

  /* 3. Deadline in the past.  */
  d = deadline_ms (-1000);
  t0 = ms_now ();
  r = sem_timedwait (&s1, &d);
  e = errno;
  el = ms_now () - t0;
  result ("past deadline: ETIMEDOUT at once (ms)",
	  r == -1 && e == ETIMEDOUT && el < 20, "%.0f", el);
  sem_post (&s1);
  r = sem_timedwait (&s1, &d);
  result ("past deadline, count 1: 0", r == 0, "%.0f", 0);

  /* 4. Bad tv_nsec.  */
  d = deadline_ms (100);
  d.tv_nsec = 1000000000L;
  r = sem_timedwait (&s1, &d);
  e = errno;
  result ("tv_nsec 1000000000: EINVAL", r == -1 && e == EINVAL, "%.0f", 0);

  /* 5. A waiting thread takes no processor time: the main thread counts
     as fast with a thread blocked in sem_wait as with none.  */
  unsigned long alone = count_for (500);
  pthread_create (&t, NULL, wait_s2, NULL);
  usleep (50000);
  unsigned long with_waiter = count_for (500);
  sem_post (&s2);
  pthread_join (t, NULL);
  double ratio = (double) with_waiter / (double) alone;
  result ("a sem_wait thread takes no time (speed %)", ratio > 0.8,
	  "%.0f", ratio * 100);

  /* 6. 100000 waits and posts between two threads (each sem_wait used to
     leak a small block; now nothing is allocated): all handed over.  */
  t0 = ms_now ();
  pthread_create (&t, NULL, pingpong, (void *) 100000L);
  for (long i = 0; i < 100000; i++)
    {
      sem_post (&s1);
      sem_wait (&s2);
    }
  pthread_join (t, NULL);
  el = ms_now () - t0;
  int v1, v2;
  sem_getvalue (&s1, &v1);
  sem_getvalue (&s2, &v2);
  result ("100000 waits and posts: done in (ms)", v1 == 0 && v2 == 0,
	  "%.0f", el);

  /* 7. Every thread waiting; a SIGALRM handler posts after 300 ms.  */
  pthread_create (&u, NULL, wait_s2, NULL);
  signal (SIGALRM, on_alarm);
  struct itimerval it = { { 0, 0 }, { 0, 300000 } };
  t0 = ms_now ();
  setitimer (ITIMER_REAL, &it, NULL);
  r = sem_wait (&s1);
  el = ms_now () - t0;
  result ("all waiting, posted by a signal handler (ms)",
	  r == 0 && el >= 250 && el <= 600, "%.0f", el);
  sem_post (&s2);
  pthread_join (u, NULL);

  printf ("%s\n", bad ? "FAIL" : "PASS");
  return bad;
}
