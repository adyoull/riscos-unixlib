/* Mutex routines. Most of the functionality is implemented in lock.c.
   Written by Alex Waugh.
   Copyright (c) 2002, 2003, 2004, 2005, 2006 UnixLib Developers.  */

#include <errno.h>
#include <pthread.h>

#include <internal/unix.h>

/* 2026 (P10): __pthread_disable_ints and __pthread_enable_ints
   (pthread/_ints.s) in line, for the fast paths below: the same
   operations on the same word (an exclusive increment, a plain
   decrement), with compiler barriers so that nothing between them is
   moved outside.  */
static inline void
hold_switching (void)
{
  __atomic_fetch_add (&__ul_global.pthread_callevery_rma->pthread_worksemaphore,
		      1, __ATOMIC_RELAXED);
  __asm__ volatile ("" ::: "memory");
}

static inline void
release_switching (void)
{
  __asm__ volatile ("" ::: "memory");
  __ul_global.pthread_callevery_rma->pthread_worksemaphore--;
}

/* Initialise a mutex object with the specified attributes */
int
pthread_mutex_init (pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
  if (mutex == NULL)
    return EINVAL;

  mutex->count = 0;
  mutex->owner = NULL;
  mutex->type = LOCK_MUTEX;
  mutex->waiting = NULL;

  if (attr != NULL)
    {
      /* Use supplied attributes */
      mutex->attr = *attr;
    }
  else
    {
      /* Use default attributes */
      mutex->attr.pshared = PTHREAD_PROCESS_PRIVATE;
      mutex->attr.type = PTHREAD_MUTEX_DEFAULT;
    }

  return 0;
}

/* Destroy a mutex that is finished with */
int
pthread_mutex_destroy (pthread_mutex_t *mutex)
{
  if (mutex == NULL)
    return EINVAL;

  if (mutex->type != LOCK_MUTEX)
    return EINVAL;

  if (mutex->count != 0 || mutex->waiting != NULL)
    return EBUSY;

  mutex->type = LOCK_INVALID;

  return 0;
}

/* Try to lock a mutex, but don't block if it is unavailable */
int
pthread_mutex_trylock (pthread_mutex_t *mutex)
{
  if (mutex == NULL || mutex->type != LOCK_MUTEX)
    return EINVAL;

  return __pthread_lock_lock (mutex, LOCK_MUTEX, 1);
}

/* Lock a mutex, blocking if it is not available */
int
pthread_mutex_lock (pthread_mutex_t *mutex)
{
  if (mutex == NULL || mutex->type != LOCK_MUTEX)
    return EINVAL;

  /* 2026 (P10): the common case, a free mutex, without the general code
     (lock.c): with switching held off, take it as __pthread_lock_acquire
     does.  Anything else (held by this thread or another) goes the old
     way, which looks at the mutex again.  */
  hold_switching ();
  if (mutex->owner == NULL)
    {
      mutex->owner = __pthread_running_thread;
      mutex->count++;
      release_switching ();
      return 0;
    }
  release_switching ();

  return __pthread_lock_lock (mutex, LOCK_MUTEX, 0);
}

/* Unlock a previously locked mutex */
int
pthread_mutex_unlock (pthread_mutex_t *mutex)
{
  /* 2026 (P10): the common case, a mutex this thread holds once with
     nobody waiting for it, without the general code: with switching held
     off, give it up as __pthread_lock_unlock does (there is nobody to
     wake, and so no yield).  Anything else goes the old way.  */
  if (mutex != NULL)
    {
      hold_switching ();
      if (mutex->type == LOCK_MUTEX
	  && mutex->owner == __pthread_running_thread
	  && mutex->count == 1 && mutex->waiting == NULL)
	{
	  mutex->count = 0;
	  mutex->owner = NULL;
	  release_switching ();
	  return 0;
	}
      release_switching ();
    }

  return __pthread_lock_unlock (mutex,1);
}
