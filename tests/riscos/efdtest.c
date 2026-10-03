/* efdtest: eventfd used between threads (riscos-unixlib, 2026-10-03).
   Two threads write 1 to an eventfd as fast as they can for about 10
   seconds while the main thread reads it (non-blocking); every write must
   be counted exactly once.  Then a blocking read must wake when another
   thread writes (rc1 stopped there: "pthread_yield called with context switching disabled").  Before 5.0.3.2-rc1 the counter's load and store could
   be split by a thread switch, losing counts (GLib's main-loop wakeups
   use an eventfd).  */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/eventfd.h>

static int efd;
static volatile int stop;

static void *writer (void *arg)
{
  unsigned long long *n = arg;
  while (!stop)
    {
      if (eventfd_write (efd, 1) == 0)
	(*n)++;
    }
  return NULL;
}

static void *late_writer (void *arg)
{
  (void) arg;
  usleep (500000);
  eventfd_write (efd, 42);
  return NULL;
}

int main (void)
{
  pthread_t t1, t2;
  unsigned long long n1 = 0, n2 = 0, got = 0;
  eventfd_t v;
  time_t end;
  int bad = 0;

  efd = eventfd (0, EFD_NONBLOCK);
  if (efd < 0)
    {
      perror ("eventfd");
      return 1;
    }
  pthread_create (&t1, NULL, writer, &n1);
  pthread_create (&t2, NULL, writer, &n2);
  end = time (NULL) + 10;
  while (time (NULL) < end)
    if (eventfd_read (efd, &v) == 0)
      got += v;
  stop = 1;
  pthread_join (t1, NULL);
  pthread_join (t2, NULL);
  if (eventfd_read (efd, &v) == 0)
    got += v;
  printf ("written %llu (%llu + %llu), read %llu: %s\n", n1 + n2, n1, n2,
	  got, got == n1 + n2 ? "ok" : "FAIL (counts lost or doubled)");
  bad |= got != n1 + n2 || n1 == 0 || n2 == 0;
  close (efd);

  /* A blocking read wakes when another thread writes.  */
  efd = eventfd (0, 0);
  pthread_create (&t1, NULL, late_writer, NULL);
  v = 0;
  if (eventfd_read (efd, &v) != 0 || v != 42)
    bad = 1;
  pthread_join (t1, NULL);
  printf ("blocking read woken by another thread: %s\n",
	  v == 42 ? "ok" : "FAIL");
  close (efd);

  printf ("%s\n", bad ? "FAIL" : "PASS");
  return bad;
}
