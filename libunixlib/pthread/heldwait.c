/* __pthread_held_wait ()
   Copyright (c) 2026 UnixLib Developers.

   2026: library code that has to wait for another thread from inside a
   call that holds thread switching off (write () with
   __pthread_disable_ints, read () / readv () / writev () and stdio with
   PTHREAD_UNSAFE) can't just call pthread_yield (): that is a fatal error
   ("pthread_yield called with context switching disabled").  Releasing
   one level of the hold and yielding (as dsp.c used to) is only right
   when that is the only level: through stdio the hold is 2 deep (fwrite
   is PTHREAD_UNSAFE, then write () takes another), and the yield was
   still fatal.

   So:
     - threads not running: nothing to yield to, return 1;
     - hold 0: yield, return 1;
     - hold 1 (the call's own hold): release it around the yield, return 1;
     - hold 2 or more: the caller is inside something else that holds
       switching off (stdio, a library lock); don't yield, return 0.  The
       caller must not then wait for another thread for ever.
     - in a signal handler: as for a nested hold, return 0.

   With CANCEL set, the wait is a cancellation point (blocking read and
   write are, in POSIX).

   The hold's return address (__pthread_protect_unsafe) is a single
   global, not per thread: another thread's PTHREAD_UNSAFE call replaces
   it while this one waits.  It is kept here and put back once switching
   is held off again.

   Known limit: a hold of 1 can be a PTHREAD_UNSAFE caller's rather than
   the call's own (nested PTHREAD_UNSAFE doesn't add to the hold, so
   fread () -> read () is 1 deep).  Releasing it lets other threads run
   while that caller is part way through; a blocking read through a FILE
   from two threads at once is the case that could notice.  */

#include <stddef.h>
#include <pthread.h>

#include <internal/unix.h>

int
__pthread_held_wait (int cancel)
{
  struct ul_global *gbl = &__ul_global;

  if (!gbl->pthread_system_running)
    return 1;

  /* In a signal handler the hold includes the handler's own, and other
     threads mustn't run while executing_signalhandler (a global) is set:
     treat it as nested.  */
  if (gbl->executing_signalhandler)
    return 0;

  int held = gbl->pthread_callevery_rma->pthread_worksemaphore;
  if (held == 0)
    {
      if (cancel)
	pthread_testcancel ();
      pthread_yield ();
      return 1;
    }
  if (held != 1)
    return 0;

  /* Empty while released: in __UNIXLIB_PARANOID builds another thread's
     PTHREAD_UNSAFE call checks that it is (and if this thread is
     cancelled below, nothing stale is left).  */
  void *ret = gbl->pthread_return_address;
  gbl->pthread_return_address = NULL;
  __pthread_enable_ints ();
  if (cancel)
    pthread_testcancel ();
  pthread_yield ();
  __pthread_disable_ints ();
  gbl->pthread_return_address = ret;
  return 1;
}
