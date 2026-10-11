/* Thread cancellation functions.
   Written by Alex Waugh.
   Copyright (c) 2002, 2003, 2004, 2005, 2006, 2008 UnixLib Developers.  */

#include <errno.h>

#include <internal/os.h>
#include <pthread.h>


/* Set the cancelability state of the current thread */
int
pthread_setcancelstate (int state, int *oldstate)
{
  if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE)
    return EINVAL;

  __pthread_disable_ints ();

  if (oldstate != NULL)
    *oldstate = __pthread_running_thread->cancelstate;

  __pthread_running_thread->cancelstate = state;

  __pthread_enable_ints ();
  return 0;
}

/* Set the type of cancellation that may occur on the current thread */
int
pthread_setcanceltype (int type, int *oldtype)
{
  if (type != PTHREAD_CANCEL_DEFERRED && type != PTHREAD_CANCEL_ASYNCHRONOUS)
    return EINVAL;

  __pthread_disable_ints ();

  if (oldtype != NULL)
    *oldtype = __pthread_running_thread->canceltype;

  __pthread_running_thread->canceltype = type;

  __pthread_enable_ints ();
  return 0;
}

/* Cancel a specific thread */
int
pthread_cancel (pthread_t thread)
{
  if (__pthread_invalid (thread))
    return ESRCH;

  __pthread_disable_ints ();

  thread->cancelpending = 1;

  /* 2026: a thread waiting on a condition variable is woken (a spurious
     wakeup, which POSIX allows), so that a wait that is a cancellation
     point (sem_wait, which now waits on one instead of polling: P5) can
     act on it.  Taken off the list as pthread_cond_signal does; its
     'cond' stays set, so a timed wait reports 0, not ETIMEDOUT.  */
  if (thread->cancelstate == PTHREAD_CANCEL_ENABLE
      && (thread->state == STATE_COND_WAIT
	  || thread->state == STATE_COND_TIMED_WAIT)
      && thread->cond != NULL)
    {
      pthread_t *link;

      for (link = &thread->cond->waiting; *link; link = &(*link)->nextwait)
	if (*link == thread)
	  {
	    *link = thread->nextwait;
	    break;
	  }
      thread->state = STATE_RUNNING;
    }

  __pthread_enable_ints ();

  return 0;
}
