/* POSIX Semaphores.
   Copyright (c) 2004-2008 UnixLib Developers.  */

/*-
 * Copyright (c) 2003 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (C) 2000 Jason Evans <jasone@freebsd.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice(s), this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified other than the possible
 *    addition of one or more copyright notices.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice(s), this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* #define PTHREAD_DEBUG_SEMS */

#include <sys/cdefs.h>

#include <sys/types.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdarg.h>

#include <stddef.h>
#include <time.h>

#include <pthread.h>
#include <internal/unix.h>

#ifdef PTHREAD_DEBUG_SEMS
#include <internal/os.h>
#endif

/* 2026 (riscos-mesa): waiting threads block, as on a condition variable.

   The threads waiting on a semaphore are kept the way a condition
   variable keeps its waiters: the first in usem_waiters, the rest linked
   through their nextwait, each in STATE_COND_TIMED_WAIT with its cond
   pointing at the queue.  usem_waiters is viewed as the `waiting' member
   of a pthread_cond_t, so the context switcher (pthread/context.c), which
   unlinks a thread whose wait has timed out, and fork (sys/vfork.c),
   which unlinks threads that don't exist in the child, both work on it
   unchanged.  Only `waiting' is ever used through that view; sem_t's
   layout doesn't change.

   A waiter always has a timeout: its deadline, or at most SEM_RECHECK_CS
   from now.  So a sem_post from a signal handler, or a pending
   cancellation, is seen within that time, and a program whose threads
   all wait on semaphores doesn't stop with "Deadlocked", as with the old
   polling sem_wait.  Waking to check costs next to nothing.

   The queue is only changed by threads, with context switches off
   (__pthread_disable_ints), as in cond.c, and by the context switcher.
   A signal handler can run at any point in a thread, even with context
   switches off, so a sem_post from a handler doesn't touch the queue: it
   only adds to the count, atomically, and the waiters see it at their
   next check.  The count is taken with a compare-and-swap.  */

#define SEM_RECHECK_CS 10

_Static_assert (offsetof (pthread_cond_t, waiting) == 0,
		"a semaphore's queue is a pthread_cond_t's waiting member");
_Static_assert (sizeof (((sem_t *) 0)->usem_waiters) == sizeof (pthread_t),
		"usem_waiters holds a pthread_t");

static __inline pthread_cond_t *
sem_queue (sem_t *sem)
{
  return (pthread_cond_t *) (void *) &sem->usem_waiters;
}

#define	USEM_MAGIC	0x09fa4012
#define	USEM_USER	0	/* assumes kernel does not use NULL */

/* Check validity of semaphore structure, return 0 if valid
   and non-zero if invalid.  */
static __inline int sem_check_validity (const sem_t *sem)
{
  if (sem != NULL && sem->usem_magic == USEM_MAGIC)
    return 0;

  return __set_errno (EINVAL);
}

static __inline void sem_free (sem_t *sem)
{
#ifdef PTHREAD_DEBUG_SEMS
  __os_print("-- sem_free: Freeing semaphore (");
  __os_prhex((int)sem);
  __os_print(")\r\n");
#endif
  sem->usem_magic = 0;
}

/* Initialise a semaphore structure.  User has already passed a valid
   piece of memory in 'sem'.  */
int sem_init (sem_t *sem, int pshared, unsigned int value)
{
  semid_t semid = USEM_USER;

  /* Semaphores shared between processes aren't supported on RISC OS */
  if (pshared)
    return __set_errno (ENOSYS);

  if (value > SEM_VALUE_MAX)
    return __set_errno (EINVAL);

#ifdef PTHREAD_DEBUG_SEMS
  __os_print("-- sem_alloc: Allocating semaphore (");
  __os_prhex((int)sem);
  __os_print(" id: ");
  __os_prhex((int)semid);
  __os_print(" value: ");
  __os_prdec((int)value);
  __os_print(")\r\n");
#endif
  sem->usem_magic = USEM_MAGIC;
  pthread_rwlock_init(&sem->usem_interlock, NULL);
  sem_queue (sem)->waiting = NULL;
  sem->usem_count = value;
  sem->usem_semid = semid;
  return 0;
}

int
sem_destroy (sem_t *sem)
{
  if (sem_check_validity (sem))
    return -1;

  __pthread_disable_ints ();
  if (sem_queue (sem)->waiting != NULL)
    {
      __pthread_enable_ints ();
      return __set_errno (EBUSY);
    }
  __pthread_enable_ints ();

  sem_free (sem);
  return 0;
}

sem_t *
sem_open(const char *name, int oflag, ...)
{
  /* not supported under RISC OS */
  __set_errno (ENOSYS);
  return SEM_FAILED;
}

int
sem_close(sem_t *sem)
{
  /* not supported under RISC OS */
  return __set_errno (ENOSYS);
}

int
sem_unlink(const char *name)
{
  /* not supported under RISC OS */
  errno = ENOSYS;
  return (-1);
}

/* Can this thread let others run (pthread_yield)?  Not without the
   thread system, nor in a signal handler or with context switching held
   off (pthread_yield would stop the program with "pthread_yield called
   with context switching disabled"): then a waiter polls, and sem_post
   doesn't yield.  sem_post is async-signal-safe in POSIX.  */
static __inline int
sem_can_switch (void)
{
  const struct ul_global *gbl = &__ul_global;

  return gbl->pthread_system_running
    && !gbl->executing_signalhandler
    && gbl->pthread_callevery_rma->pthread_worksemaphore == 0;
}

/* Add N to the count, atomically even against a signal handler.  */
static __inline void
sem_add (sem_t *sem, int n)
{
#if __GCC_HAVE_SYNC_COMPARE_AND_SWAP_4
  __atomic_fetch_add (&sem->usem_count, n, __ATOMIC_SEQ_CST);
#else
  __atomic_modify (&sem->usem_count, n);	/* IRQs off around it */
#endif
}

/* Take the semaphore if its count is above 0: 1 if taken.  A
   compare-and-swap where there is one, so even a handler's sem_trywait
   (not async-signal-safe, but programs do it) can't make the count wrap.
   Otherwise, check then decrement: right as long as only threads, with
   context switches off, lower the count.  */
static __inline int
sem_take (sem_t *sem)
{
#if __GCC_HAVE_SYNC_COMPARE_AND_SWAP_4
  unsigned int v = __atomic_load_n (&sem->usem_count, __ATOMIC_SEQ_CST);
  while (v != 0)
    if (__atomic_compare_exchange_n (&sem->usem_count, &v, v - 1, 0,
				     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
      return 1;
  return 0;
#else
  if (sem->usem_count == 0)
    return 0;
  sem_add (sem, -1);
  return 1;
#endif
}

/* Remove SELF from the semaphore's queue if it is still there.  Called
   with context switches off.  */
static void
sem_unqueue (sem_t *sem, pthread_t self)
{
  pthread_t *link;

  for (link = &sem_queue (sem)->waiting; *link; link = &(*link)->nextwait)
    if (*link == self)
      {
	*link = self->nextwait;
	break;
      }
  self->nextwait = NULL;
  self->cond = NULL;
}

/* Wait for the semaphore, until ABSTIME (CLOCK_REALTIME) if not NULL.
   0, or -1 with errno ETIMEDOUT.  */
static int
sem_wait_until (sem_t *sem, const struct timespec *abstime)
{
  for (;;)
    {
      __pthread_disable_ints ();
      if (sem_take (sem))
	{
	  __pthread_enable_ints ();
	  return 0;
	}
      __pthread_enable_ints ();

      /* A cancellation point, acted on only while the count is 0, so a
	 post meant for this thread isn't lost.  */
      if (__ul_global.pthread_system_running)
	pthread_testcancel ();

      clock_t wait_cs = SEM_RECHECK_CS;
      if (abstime != NULL)
	{
	  struct timespec now;
	  if (clock_gettime (CLOCK_REALTIME, &now) != 0)
	    return -1;
	  if (now.tv_sec > abstime->tv_sec
	      || (now.tv_sec == abstime->tv_sec
		  && now.tv_nsec >= abstime->tv_nsec))
	    return __set_errno (ETIMEDOUT);
	  /* The time left, in centiseconds rounded up (at least 1), when
	     it's under 2 s (so it fits a long); SEM_RECHECK_CS is less.  */
	  if (abstime->tv_sec - now.tv_sec < 2)
	    {
	      long left_ns = (long) (abstime->tv_sec - now.tv_sec) * 1000000000L
			     + (abstime->tv_nsec - now.tv_nsec);
	      long left_cs = (left_ns + 9999999) / 10000000;
	      if (left_cs < wait_cs)
		wait_cs = (clock_t) left_cs;
	    }
	}

      if (!sem_can_switch ())
	{
	  /* With the thread system running, waiting with context
	     switching held off (or in a signal handler) can't end: stop
	     with "pthread_yield called with context switching disabled",
	     as before.  A timed wait just checks again until its time is
	     up.  Without the thread system, only a signal handler can
	     post: check again.  */
	  if (__ul_global.pthread_system_running && abstime == NULL)
	    pthread_yield ();
	  continue;
	}

      pthread_t self = __pthread_running_thread;
      __pthread_disable_ints ();
      if (sem_take (sem))
	{
	  __pthread_enable_ints ();
	  return 0;
	}
      /* Join the end of the queue and sleep until sem_post wakes this
	 thread or WAIT_CS have passed (the context switcher then takes
	 it off the queue).  */
      pthread_t *link = &sem_queue (sem)->waiting;
      while (*link)
	link = &(*link)->nextwait;
      *link = self;
      self->nextwait = NULL;
      self->cond = sem_queue (sem);
      /* The context switcher wakes a thread when clock () is past
	 condtimeout, so this is WAIT_CS from now.  */
      self->condtimeout = clock () + wait_cs - 1;
      self->state = STATE_COND_TIMED_WAIT;
      __pthread_enable_ints ();

      pthread_yield ();

      __pthread_disable_ints ();
      sem_unqueue (sem, self);
      __pthread_enable_ints ();
    }
}

int
sem_wait (sem_t *sem)
{
  if (sem_check_validity (sem))
    return -1;

  PTHREAD_SAFE_CANCELLATION

  return sem_wait_until (sem, NULL);
}

int
sem_timedwait (sem_t *sem, const struct timespec *abstime)
{
  if (sem_check_validity (sem))
    return -1;

  PTHREAD_SAFE_CANCELLATION

  /* POSIX: abstime isn't checked when the semaphore can be taken.  */
  __pthread_disable_ints ();
  if (sem_take (sem))
    {
      __pthread_enable_ints ();
      return 0;
    }
  __pthread_enable_ints ();

  if (abstime == NULL || abstime->tv_nsec < 0
      || abstime->tv_nsec >= 1000000000)
    return __set_errno (EINVAL);

  return sem_wait_until (sem, abstime);
}

int
sem_trywait (sem_t *sem)
{
  if (sem_check_validity (sem))
    return -1;

  __pthread_disable_ints ();
  int taken = sem_take (sem);
  __pthread_enable_ints ();

  return taken ? 0 : __set_errno (EAGAIN);
}

int
sem_post (sem_t *sem)
{
  if (sem_check_validity (sem))
    return -1;

  if (__ul_global.executing_signalhandler)
    {
      /* In a signal handler: only the count (see the top of the file).  */
      sem_add (sem, 1);
      return 0;
    }

  __pthread_disable_ints ();
  sem_add (sem, 1);

  /* Wake the first waiter: it takes the count when it runs (or, if
     another thread has taken it by then, waits again).  */
  pthread_t blocked = sem_queue (sem)->waiting;
  if (blocked != NULL)
    {
      sem_queue (sem)->waiting = blocked->nextwait;
      blocked->nextwait = NULL;
      blocked->state = STATE_RUNNING;
    }
  __pthread_enable_ints ();

  /* As before, give the woken thread a chance to run, but only where a
     yield is allowed (it used to stop a program that posted from a
     signal handler).  */
  if (sem_can_switch ())
    pthread_yield ();
  return 0;
}

int
sem_getvalue (sem_t * __restrict sem, int * __restrict sval)
{
  if (sem_check_validity (sem))
    return -1;

  __pthread_disable_ints ();
  *sval = (int) __atomic_load_n (&sem->usem_count, __ATOMIC_SEQ_CST);
  __pthread_enable_ints ();

  return 0;
}
