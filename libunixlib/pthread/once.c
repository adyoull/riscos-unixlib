/* Written by Alex Waugh.
   Copyright (c) 2002, 2003, 2004, 2005, 2006 UnixLib Developers.  */

#include <errno.h>
#include <pthread.h>

/* 2026: once_mutex only protects the state of the pthread_once_t objects,
   and once_cond signals changes of that state.  It is NOT held while an
   init routine runs.

   It used to be held, one mutex for every pthread_once_t, for the whole of
   the init routine.  An init routine that waits for another thread which
   itself calls pthread_once (for a different pthread_once_t) then
   deadlocked: the C++ library does exactly that in std::async
   (future::get runs call_once (thread::join) while the other thread runs
   call_once (set the result)).  Found and fixed by Rob Heaton
   (riscos-gcc16, his report 10); this is his design, without the C++
   exception handling (see below), and with a fast path for an object
   whose init routine has already run.

   State of a pthread_once_t:
     0 (PTHREAD_ONCE_INIT)  the init routine hasn't run
     ONCE_DONE              it has completed (1, as before)
     ONCE_RUNNING           a thread is in it

   A C++ exception thrown out of the init routine isn't handled here (as
   before): handling it needs this file compiled with -fexceptions, which
   would pull the C++ unwinder into every C program that calls
   pthread_once.  */
#define ONCE_DONE    1
#define ONCE_RUNNING 2

static pthread_mutex_t once_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t once_cond = PTHREAD_COND_INITIALIZER;

/* Cleanup handler for pthread_once: the init routine was cancelled, or
   called pthread_exit.  The effect must be as if pthread_once was never
   called, so another thread may try.  */
static void
__pthread_once_cleanup (void *arg)
{
  pthread_once_t *once_control = arg;

  pthread_mutex_lock (&once_mutex);
  *once_control = 0;
  pthread_cond_broadcast (&once_cond);
  pthread_mutex_unlock (&once_mutex);
}

/* Ensure a specified function is called exactly once.  A thread that
   finds the init routine running in another thread waits until that has
   finished (or been cancelled), as POSIX requires.  */
int
pthread_once (pthread_once_t *once_control, void (*init_routine) (void))
{
  if (once_control == NULL || init_routine == NULL)
    return EINVAL;

  /* Already done: nothing to wait for.  The value is only ever set to
     ONCE_DONE after the init routine has returned.  */
  if (*(volatile pthread_once_t *) once_control == ONCE_DONE)
    return 0;

  pthread_mutex_lock (&once_mutex); /* This won't fail */

  while (*once_control == ONCE_RUNNING)
    pthread_cond_wait (&once_cond, &once_mutex);

  if (*once_control == 0)
    {
      *once_control = ONCE_RUNNING;
      pthread_mutex_unlock (&once_mutex);

      pthread_cleanup_push (__pthread_once_cleanup, once_control);

      init_routine ();

      pthread_cleanup_pop (0);

      pthread_mutex_lock (&once_mutex);
      *once_control = ONCE_DONE;
      pthread_cond_broadcast (&once_cond);
    }

  pthread_mutex_unlock (&once_mutex);

  return 0;
}
