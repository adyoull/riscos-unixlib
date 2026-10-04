/* fwtest: fork from a program whose worker thread is running
   (riscos-unixlib, 5.0.3.3-rc1; the 2026-10-04 audit).

   1. Only the forking thread exists in the child (POSIX).  The worker's
      stack is an ARMEABISupport stack, outside what fork copies, so it is
      the parent's own: a child that let the worker run (here: an atexit
      handler that joins it, as SDL_Quit does) used to run it on the
      parent's stack, and if it finished there, free that stack from under
      the parent.  Now the child's join gives ESRCH at once and the worker
      never runs in the child.
   2. An atfork prepare handler that changes the forking thread's caller's
      frame: the stack copy fork keeps for the parent used to be taken
      before the handlers ran, so the parent got the old value back.

   The parent checks that its worker is still running and unharmed after
   the child has gone, then stops and joins it.  Must print PASS.  */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static volatile int stop;
static volatile unsigned long ticks;
static pthread_t worker;
static volatile int *frame_value;	/* a variable in main's frame */
static int in_child;
static int child_join_rc = -1;

static void *work (void *arg)
{
  volatile char pattern[256];
  (void) arg;
  memset ((void *) pattern, 0x5A, sizeof pattern);
  while (!stop)
    {
      ticks++;
      for (size_t i = 0; i < sizeof pattern; i++)
	if (pattern[i] != 0x5A)
	  return (void *) 1;		/* its stack was changed under it */
      sched_yield ();
    }
  return NULL;
}

static void prepare (void)
{
  *frame_value = 42;
}

static void child_atexit (void)
{
  if (!in_child)
    return;
  /* As SDL_Quit -> SDL_WaitThread would: join the parent's thread.  */
  stop = 1;
  child_join_rc = pthread_join (worker, NULL);
  if (child_join_rc != ESRCH)
    _exit (10 + (child_join_rc & 0x3f));
}

int main (void)
{
  volatile int value = 0;
  int status, bad = 0;
  void *wret;

  frame_value = &value;
  pthread_atfork (prepare, NULL, NULL);
  atexit (child_atexit);
  if (pthread_create (&worker, NULL, work, NULL) != 0)
    {
      printf ("pthread_create failed\n");
      return 1;
    }
  while (ticks < 1000)
    sched_yield ();

  pid_t pid = fork ();
  if (pid == 0)
    {
      in_child = 1;
      /* A yield: with the worker gone this comes straight back.  */
      unsigned long t = ticks;
      for (int i = 0; i < 100; i++)
	sched_yield ();
      if (ticks != t)
	_exit (3);			/* the worker ran in the child */
      exit (0);			/* runs child_atexit: join -> ESRCH */
    }
  if (pid < 0)
    {
      printf ("fork failed: %s\n", strerror (errno));
      return 1;
    }
  waitpid (pid, &status, 0);
  in_child = 0;

  printf ("child: %s (status %d)\n",
	  WIFEXITED (status) && WEXITSTATUS (status) == 0 ? "ok"
	  : WEXITSTATUS (status) == 3 ? "FAIL (worker ran in the child)"
	  : "FAIL (join in the child didn't give ESRCH)", WEXITSTATUS (status));
  bad |= !(WIFEXITED (status) && WEXITSTATUS (status) == 0);

  printf ("atfork prepare's change to main's frame kept: %s (%d)\n",
	  value == 42 ? "ok" : "FAIL", value);
  bad |= value != 42;

  unsigned long t = ticks;
  for (int i = 0; i < 1000 && ticks == t; i++)
    sched_yield ();
  printf ("worker still running after the fork: %s\n",
	  ticks != t ? "ok" : "FAIL");
  bad |= ticks == t;

  stop = 1;
  pthread_join (worker, &wret);
  printf ("worker's stack unchanged: %s\n", wret == NULL ? "ok" : "FAIL");
  bad |= wret != NULL;

  printf ("%s\n", bad ? "FAIL" : "PASS");
  return bad;
}
