/* Host test for pthread/sem.c (2026, riscos-mesa): sem_wait blocks and
   sem_timedwait works.

   No threads here: time is simulated in centiseconds, and pthread_yield
   models what the context switcher (pthread/context.c) does for the
   waiting thread: time passes, scripted "other thread" actions run (a
   sem_post, say), and a thread in STATE_COND_TIMED_WAIT whose timeout has
   passed is taken off its queue and made runnable, with the same code as
   context.c.  */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <semaphore.h>
#include <internal/unix.h>

static struct __pthread_callevery_block rma;
struct ul_global __ul_global = { 1, 0, &rma };
static struct __pthread_thread self_thread = { STATE_RUNNING, 0, 0, 0 };
pthread_t __pthread_running_thread = &self_thread;

static long now_cs;			/* simulated time */
static int ints_off, yields, cancel_checks;
static int in_switcher, fatal_yields;

clock_t clock (void) { return now_cs; }

int
clock_gettime (clockid_t id, struct timespec *tp)
{
  (void) id;
  /* Polling (no thread can run): time passes as it does on the machine.  */
  if (__ul_global.executing_signalhandler || !__ul_global.pthread_system_running)
    now_cs++;
  tp->tv_sec = 1000000 + now_cs / 100;	/* CLOCK_REALTIME: csec resolution */
  tp->tv_nsec = (now_cs % 100) * 10000000L;
  return 0;
}

/* An absolute CLOCK_REALTIME time CS centiseconds from now.  */
static struct timespec
in_cs (long cs)
{
  struct timespec t;
  long at = now_cs + cs;
  t.tv_sec = 1000000 + at / 100;
  t.tv_nsec = (at % 100) * 10000000L;
  return t;
}

/* "Signal handler": runs from __pthread_enable_ints when the thread system
   isn't running, after HOOK_AFTER calls.  */
static void (*hook) (void);
static int hook_after;

int __pthread_disable_ints (void) { ints_off++; return 0; }
int
__pthread_enable_ints (void)
{
  ints_off--;
  if (hook && --hook_after == 0)
    {
      void (*h) (void) = hook;
      hook = 0;
      h ();
    }
  return 0;
}
int pthread_rwlock_init (pthread_rwlock_t *l, const void *a) { (void) l; (void) a; return 0; }
void pthread_testcancel (void) { cancel_checks++; }

/* Scripted actions of "another thread", at simulated times.  */
static struct { long at; void (*fn) (void); } actions[4];
static int nactions;

static void
at (long cs, void (*fn) (void))
{
  actions[nactions].at = now_cs + cs;
  actions[nactions++].fn = fn;
}

void
pthread_yield (void)
{
  if (!__ul_global.pthread_system_running || in_switcher)
    return;
  if (__ul_global.executing_signalhandler || rma.pthread_worksemaphore)
    {
      fatal_yields++;			/* the real one stops the program */
      return;
    }
  yields++;
  in_switcher = 1;
  pthread_t t = __pthread_running_thread;
  do
    {
      now_cs++;
      for (int i = 0; i < nactions; i++)
	if (actions[i].fn && now_cs >= actions[i].at)
	  {
	    void (*fn) (void) = actions[i].fn;
	    actions[i].fn = 0;
	    fn ();
	  }
      /* As context.c, for a timed wait.  */
      if (t->state == STATE_COND_TIMED_WAIT && clock () > t->condtimeout)
	{
	  t->state = STATE_RUNNING;
	  if (t->cond->waiting == t)
	    t->cond->waiting = t->nextwait;
	  else
	    {
	      pthread_t thread = t->cond->waiting;
	      while (thread->nextwait && thread->nextwait != t)
		thread = thread->nextwait;
	      if (thread->nextwait)
		thread->nextwait = thread->nextwait->nextwait;
	    }
	}
      if (now_cs > 100000)
	{
	  printf ("FAIL: the wait never ended\n");
	  t->state = STATE_RUNNING;
	}
    }
  while (t->state < STATE_RUNNING);
  in_switcher = 0;
}

static sem_t sem;
static int checks, failures;

static void
check (const char *what, int ok)
{
  checks++;
  if (!ok)
    {
      failures++;
      printf ("FAIL: %s\n", what);
    }
}

static void post (void) { sem_post (&sem); }

/* A sem_post from a signal handler.  */
static void
handler_post (void)
{
  int was = __ul_global.executing_signalhandler;
  __ul_global.executing_signalhandler = 1;
  int y = yields;
  sem_post (&sem);
  check ("a handler's post doesn't touch the queue or yield",
	 yields == y && fatal_yields == 0);
  __ul_global.executing_signalhandler = was;
}

static int
value (void)
{
  int v;
  sem_getvalue (&sem, &v);
  return v;
}

/* After every wait: no thread queued, running, interrupts balanced.  */
static void
clean (const char *what)
{
  char buf[128];
  snprintf (buf, sizeof buf, "%s: left clean", what);
  check (buf, sem.usem_waiters == NULL && self_thread.state == STATE_RUNNING
	 && self_thread.nextwait == NULL && ints_off == 0);
}

static void
reset (unsigned count)
{
  sem_init (&sem, 0, count);
  nactions = 0;
  yields = 0;
  now_cs = 0;
}

static struct __pthread_thread other = { STATE_RUNNING, 0, 0, 0 };

static void
destroy_busy (void)
{
  check ("sem_destroy with a waiter: EBUSY",
	 sem_destroy (&sem) == -1 && errno == EBUSY);
  sem_post (&sem);
}

int
main (void)
{
  int r;
  struct timespec t;

  /* Count above 0: taken at once.  */
  reset (1);
  r = sem_wait (&sem);
  check ("sem_wait, count 1: 0 at once", r == 0 && value () == 0 && yields == 0);
  clean ("count 1");

  /* Timeout: count 0, deadline in 20 cs.  */
  reset (0);
  t = in_cs (20);
  r = sem_timedwait (&sem, &t);
  check ("timeout: -1 ETIMEDOUT", r == -1 && errno == ETIMEDOUT);
  check ("timeout: after 20 cs", now_cs == 20);
  check ("timeout: asleep, not polling (few yields)", yields <= 4);
  clean ("timeout");
  printf ("timeout: %ld cs, %d yields\n", now_cs, yields);

  /* Wake-up: deadline 200 cs, a post at 10 cs.  */
  reset (0);
  at (10, post);
  t = in_cs (200);
  r = sem_timedwait (&sem, &t);
  check ("wake-up: 0", r == 0);
  check ("wake-up: at about 10 cs", now_cs >= 10 && now_cs <= 11);
  check ("wake-up: count taken", value () == 0);
  clean ("wake-up");

  /* Deadline past: ETIMEDOUT at once; with count 1, 0 at once.  */
  reset (0);
  now_cs = 50;
  t = in_cs (-10);
  r = sem_timedwait (&sem, &t);
  check ("past deadline: ETIMEDOUT at once",
	 r == -1 && errno == ETIMEDOUT && yields == 0);
  reset (1);
  now_cs = 50;
  t = in_cs (-10);
  r = sem_timedwait (&sem, &t);
  check ("past deadline, count 1: 0", r == 0 && value () == 0);

  /* Bad tv_nsec: EINVAL only when it would wait.  */
  reset (0);
  t = in_cs (10);
  t.tv_nsec = 1000000000;
  r = sem_timedwait (&sem, &t);
  check ("bad tv_nsec: EINVAL", r == -1 && errno == EINVAL);
  t.tv_nsec = -1;
  r = sem_timedwait (&sem, &t);
  check ("negative tv_nsec: EINVAL", r == -1 && errno == EINVAL);
  reset (1);
  t.tv_nsec = 1000000000;
  r = sem_timedwait (&sem, &t);
  check ("bad tv_nsec, count 1: 0", r == 0);

  /* sem_wait with no timeout: sleeps, rechecking every 10 cs, until a
     post at 35 cs; a cancellation point while it waits.  */
  reset (0);
  at (35, post);
  cancel_checks = 0;
  r = sem_wait (&sem);
  check ("sem_wait: woken by a post at 35 cs", r == 0 && now_cs >= 35 && now_cs <= 36);
  check ("sem_wait: slept (rechecks every 10 cs, not a poll)", yields <= 5);
  check ("sem_wait: checked for cancellation", cancel_checks >= 2);
  clean ("sem_wait");
  printf ("sem_wait: %ld cs, %d yields\n", now_cs, yields);

  /* A signal handler posts while a thread waits: seen at the 10 cs
     recheck, the queue untouched.  */
  reset (0);
  at (3, handler_post);
  r = sem_wait (&sem);
  check ("handler post while waiting: taken at the recheck",
	 r == 0 && now_cs == 10 && value () == 0);
  clean ("handler post while waiting");

  /* sem_destroy while a thread waits: EBUSY.  */
  reset (0);
  at (5, destroy_busy);
  r = sem_wait (&sem);
  check ("destroy: the wait still ends", r == 0);
  clean ("destroy");

  /* FIFO: another thread is queued first; a post wakes it, not us; we
     take the count when our 10 cs recheck comes.  */
  reset (0);
  other.state = STATE_COND_TIMED_WAIT;
  other.nextwait = NULL;
  other.cond = (pthread_cond_t *) (void *) &sem.usem_waiters;
  sem.usem_waiters = (void *) &other;
  at (5, post);
  t = in_cs (100);
  r = sem_timedwait (&sem, &t);
  check ("FIFO: the first waiter was woken", other.state == STATE_RUNNING);
  check ("FIFO: we took the count at our recheck", r == 0 && now_cs >= 10 && now_cs <= 12);
  clean ("FIFO");

  /* No thread system: sem_wait polls; a "signal handler" posts.  */
  reset (0);
  __ul_global.pthread_system_running = 0;
  hook = handler_post;
  hook_after = 1000;
  r = sem_wait (&sem);
  check ("no threads: a handler's post ends the wait", r == 0 && value () == 0);
  clean ("no threads");
  __ul_global.pthread_system_running = 1;

  /* In a signal handler (switching off), sem_post mustn't yield, and a
     wait polls instead.  */
  reset (0);
  __ul_global.executing_signalhandler = 1;
  r = sem_post (&sem);
  check ("post in a signal handler: no yield", r == 0 && fatal_yields == 0 && yields == 0);
  r = sem_wait (&sem);
  check ("wait in a signal handler, count 1: 0", r == 0 && fatal_yields == 0);
  hook = post;
  hook_after = 1000;
  r = sem_wait (&sem);
  check ("sem_wait in a signal handler: pthread_yield's fatal error, as before",
	 r == 0 && fatal_yields > 0);
  fatal_yields = 0;
  t = in_cs (5);
  long t0 = now_cs;
  r = sem_timedwait (&sem, &t);
  check ("sem_timedwait in a signal handler: times out, no yield",
	 r == -1 && errno == ETIMEDOUT && fatal_yields == 0 && now_cs >= t0 + 5 && now_cs <= t0 + 6);
  __ul_global.executing_signalhandler = 0;
  rma.pthread_worksemaphore = 1;
  r = sem_post (&sem);
  check ("post with switching held off: no yield", r == 0 && fatal_yields == 0);
  rma.pthread_worksemaphore = 0;
  clean ("signal handler");
  sem_trywait (&sem);

  /* Many waits and posts: nothing allocated, nothing left queued.  */
  reset (0);
  for (int i = 0; i < 100000; i++)
    {
      sem_post (&sem);
      if (sem_wait (&sem) != 0)
	{
	  check ("100000 posts and waits", 0);
	  break;
	}
    }
  check ("100000 posts and waits: count 0", value () == 0);
  clean ("100000");

  /* sem_trywait.  */
  reset (0);
  check ("trywait, count 0: EAGAIN", sem_trywait (&sem) == -1 && errno == EAGAIN);
  sem_post (&sem);
  check ("trywait, count 1: 0", sem_trywait (&sem) == 0 && value () == 0);

  printf ("sem: %d checks, %d failed\n", checks, failures);
  return failures != 0;
}
