/* oncetest: pthread_once (riscos-unixlib, 5.0.3.4-rc1; Rob Heaton's
   riscos-gcc16 report 10).

   1. An init routine that waits for another thread which itself calls
      pthread_once (on another object): this used to deadlock, because one
      lock was held for every pthread_once_t while any init routine ran
      (std::async does this).  The old library stops with "Deadlocked".
   2. Three threads call pthread_once on one object whose init routine
      takes a while: it must run exactly once, and every caller must
      return only after it has finished.
   3. A cancelled init routine: the next call must run it again.
   Must print PASS.  */
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static pthread_once_t outer = PTHREAD_ONCE_INIT, inner = PTHREAD_ONCE_INIT;
static pthread_once_t shared = PTHREAD_ONCE_INIT, cancelled = PTHREAD_ONCE_INIT;
static volatile int inner_ran, shared_runs, shared_done, seen_done[3];
static volatile int cancel_runs;

static void init_inner (void) { inner_ran = 1; }

static void *call_inner (void *arg)
{
  (void) arg;
  pthread_once (&inner, init_inner);
  return NULL;
}

static void init_outer (void)
{
  pthread_t t;
  pthread_create (&t, NULL, call_inner, NULL);
  pthread_join (t, NULL);		/* waits for a thread in pthread_once */
}

static void init_shared (void)
{
  shared_runs++;
  usleep (300000);
  shared_done = 1;
}

static void *call_shared (void *arg)
{
  pthread_once (&shared, init_shared);
  seen_done[(int) arg] = shared_done;
  return NULL;
}

static void init_cancelled (void)
{
  cancel_runs++;
  if (cancel_runs == 1)
    pthread_exit (NULL);		/* the first attempt doesn't finish */
}

static void *call_cancelled (void *arg)
{
  (void) arg;
  pthread_once (&cancelled, init_cancelled);
  return NULL;
}

int main (void)
{
  pthread_t t[3];
  int bad = 0, i;

  pthread_once (&outer, init_outer);
  printf ("init routine waiting for a thread in pthread_once: %s\n",
	  inner_ran ? "ok" : "FAIL");
  bad |= !inner_ran;

  for (i = 0; i < 3; i++)
    pthread_create (&t[i], NULL, call_shared, (void *) i);
  for (i = 0; i < 3; i++)
    pthread_join (t[i], NULL);
  int all_saw = seen_done[0] && seen_done[1] && seen_done[2];
  printf ("three callers: init ran %d time(s), all returned after it: %s\n",
	  shared_runs, shared_runs == 1 && all_saw ? "ok" : "FAIL");
  bad |= !(shared_runs == 1 && all_saw);

  pthread_create (&t[0], NULL, call_cancelled, NULL);
  pthread_join (t[0], NULL);
  pthread_once (&cancelled, init_cancelled);
  printf ("cancelled init routine runs again: %s (%d runs)\n",
	  cancel_runs == 2 ? "ok" : "FAIL", cancel_runs);
  bad |= cancel_runs != 2;

  printf ("%s\n", bad ? "FAIL" : "PASS");
  return bad;
}
