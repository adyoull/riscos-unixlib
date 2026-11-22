/* susptest: does pthread_suspend_thread really stop a thread?
   (riscos-unixlib P9, from riscos-unixlib-web W3, 2026-10-21)

   WebKit's garbage collector suspends every other thread, reads its
   registers and scans its stack.  UnixLib's pthread_suspend_thread set a
   flag that the scheduler's 'continue' never acted on (the loop's test
   looked only at the thread's state), so a "suspended" thread kept
   running.  This checks, 50 times over:
     - a spinning thread's counter doesn't move while it's suspended
       (with the main thread sleeping, so the ticker switches threads);
     - pthread_get_thread_registers gives a stack pointer inside the
       thread's stack and a non-zero pc;
     - after pthread_resume_thread the counter moves again;
   and that a thread suspended while waiting on a condition variable
   doesn't run when signalled until it's resumed.  */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

static volatile unsigned long counter;
static volatile int stop;
static int failures;

static void *spin (void *arg)
{
  (void) arg;
  while (!stop)
    counter++;
  return NULL;
}

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t c = PTHREAD_COND_INITIALIZER;
static volatile int woken, go;

static void *waiter (void *arg)
{
  (void) arg;
  pthread_mutex_lock (&m);
  while (!go)
    pthread_cond_wait (&c, &m);
  woken = 1;
  pthread_mutex_unlock (&m);
  return NULL;
}

static void result (int ok, const char *what)
{
  printf ("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    failures++;
}

static void ms (int n)
{
  struct timespec ts = { n / 1000, (n % 1000) * 1000000L };
  nanosleep (&ts, NULL);
}

int main (void)
{
  pthread_t th;
  pthread_attr_t attr;
  pthread_create (&th, NULL, spin, NULL);
  ms (100);
  result (counter > 0, "the spinning thread runs");

  pthread_getattr_np (th, &attr);
  void *lo;
  size_t size;
  pthread_attr_getstack (&attr, &lo, &size);
  unsigned long hi = (unsigned long) lo + size;

  int still = 1, moved = 1, regs = 1;
  for (int i = 0; i < 50; i++)
    {
      if (pthread_suspend_thread (th))
	{
	  result (0, "pthread_suspend_thread returned an error");
	  break;
	}
      ms (5);	/* any switch still due happens now */
      unsigned long c1 = counter;
      ms (60);	/* the ticker switches threads several times */
      unsigned long c2 = counter;
      if (c1 != c2)
	{
	  if (still)
	    printf ("     round %d: counter moved by %lu while suspended\n", i, c2 - c1);
	  still = 0;
	}
      mcontext_t mc;
      pthread_get_thread_registers (th, &mc);
      unsigned long sp = (unsigned) mc.gregs[13], pc = (unsigned) mc.gregs[15];
      if (sp < (unsigned long) lo || sp >= hi || pc == 0)
	{
	  if (regs)
	    printf ("     round %d: sp &%08lX (stack &%08lX-&%08lX), pc &%08lX\n",
		    i, sp, (unsigned long) lo, hi, pc);
	  regs = 0;
	}
      pthread_resume_thread (th);
      ms (40);
      if (counter == c2)
	moved = 0;
    }
  result (still, "a suspended thread doesn't run (50 rounds)");
  result (regs, "its registers: sp inside its stack, pc set");
  result (moved, "it runs again after pthread_resume_thread");
  stop = 1;
  pthread_join (th, NULL);

  pthread_t w;
  pthread_create (&w, NULL, waiter, NULL);
  ms (50);
  pthread_suspend_thread (w);
  pthread_mutex_lock (&m);
  go = 1;
  pthread_cond_signal (&c);
  pthread_mutex_unlock (&m);
  ms (100);
  result (!woken, "a suspended waiter doesn't run when signalled");
  pthread_resume_thread (w);
  ms (100);
  result (woken, "it runs once resumed");
  pthread_join (w, NULL);

  printf ("susptest: %s\n", failures ? "FAILED" : "all passed");
  return failures != 0;
}
