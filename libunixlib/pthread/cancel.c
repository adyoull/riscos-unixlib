/* Thread cancellation functions.
   Written by Alex Waugh.
   Copyright (c) 2002, 2003, 2004, 2005, 2006, 2008 UnixLib Developers.  */

#include <errno.h>

#include <internal/os.h>
#include <pthread.h>

/* 2026 (P12): libgcc's emutls.o (behind __thread and C++ thread_local)
   reaches these through weak references, and uses them once
   pthread_cancel is linked (its test for "threads are in use").  In a
   static link, a program with pthread_cancel but without pthread_once
   had emutls call address 0: its key was never made, and it read
   another key's value.  Linking pthread_cancel now brings them all.  */
void *const __pthread_emutls_needs[] __attribute__ ((used)) =
{
  (void *) pthread_once,
  (void *) pthread_key_create,
  (void *) pthread_getspecific,
  (void *) pthread_setspecific,
  (void *) pthread_mutex_lock,
  (void *) pthread_mutex_unlock
};


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

  thread->cancelpending = 1;

  return 0;
}
