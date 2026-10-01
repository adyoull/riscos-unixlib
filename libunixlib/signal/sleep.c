/*
 * File taken from glibc.
 *  - SCL poison added.
 */
#ifdef __TARGET_SCL__
#  error "SCL build should not use (L)GPL code."
#endif

/* Copyright (C) 1991, 1992, 1993, 1996, 1997 Free Software Foundation, Inc.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include <internal/unix.h>
#include <pthread.h>
#include <stdint.h>

/* #define DEBUG */
#ifdef DEBUG
#  include <sys/debug.h>
#endif

#define USECS_PER_CLOCK (1000000 / CLOCKS_PER_SEC)
#define NSECS_PER_CLOCK (1000000000 / CLOCKS_PER_SEC)

/* 2026: yield to other threads only when that is allowed.  pthread_yield
   is a fatal error while thread switching is held off (worksemaphore
   non-zero), and does nothing useful before threads are started.  */
static void
safe_yield (void)
{
  if (__ul_global.pthread_system_running
      && __ul_global.pthread_callevery_rma->pthread_worksemaphore == 0)
    pthread_yield ();
}

/* 2026: the longest single sleep_int call.  ualarm takes a 32-bit count of
   microseconds, which overflows above 429496 centiseconds.  */
#define SLEEP_CHUNK ((clock_t) 400000)

/* SIGALRM signal handler for `sleep'.  This does nothing but return,
   but SIG_IGN isn't supposed to break `pause'.  */
static void
sleep_handler (int sig)
{
  sig = sig;
  return;
}

/* Make the process sleep for CLOCKTICKS clock ticks, or until a signal
   arrives and is not ignored.  The function returns the number of clock
   ticks less than CLOCKTICKS which it actually slept (zero if it slept
   the full time).
   If a signal handler does a `longjmp' or modifies the handling of the
   SIGALRM signal while inside `sleep' call, the handling of the SIGALRM
   signal afterwards is undefined.  There is no return value to indicate
   error, but if `sleep' returns SECONDS, it probably didn't work.  */
static clock_t
sleep_int (clock_t clockticks)
{
  clock_t before, after, slept, remaining;
  sigset_t set, oset;
  struct sigaction act, oact;
  int save = errno;

  if (clockticks == 0)
    return 0;

  /* alarm() does not work in a TaskWindow nor whilst running as a
     WIMP program.  */
  if (__get_taskhandle () != 0)
    {
      before = clock () + clockticks;
      while (clock () < before)
	safe_yield ();
      return 0;
    }

  /* Block SIGALRM signals while frobbing the handler.  */
  if (sigemptyset (&set) < 0
      || sigaddset (&set, SIGALRM) < 0
      || sigprocmask (SIG_BLOCK, &set, &oset))
    return clockticks;

  act.sa_handler = sleep_handler;
  act.sa_flags = 0;
  act.sa_mask = oset; /* Execute handler with original mask.  */
  if (sigaction (SIGALRM, &act, &oact))
    return clockticks;

  before = clock ();
  remaining = (ualarm ((useconds_t) clockticks * USECS_PER_CLOCK, 0)
	       / USECS_PER_CLOCK);

#ifdef DEBUG
  debug_printf ("-- sleep: Set up an alarm for %d clockticks\n"
		"   Remaining: %d clockticks\n", clockticks, remaining);
#endif

  if (remaining > 0 && remaining < clockticks)
    {
      /* The user's alarm will expire before our own would.
         Restore the user's signal action state and let his alarm happen.  */
      sigaction (SIGALRM, &oact, (struct sigaction *) NULL);

      /* Restore sooner alarm.  */
      ualarm ((useconds_t) clockticks * USECS_PER_CLOCK, 0);
#ifdef DEBUG
      debug_printf ("-- sleep: A user alarm existed. Wait %d clockticks for that instead\n", remaining);
#endif
      sigsuspend (&oset);	/* Wait for it to go off.  */
#ifdef DEBUG
      debug_printf ("-- sleep: Alarm has gone off. Continuing with execution\n");
#endif
      after = clock ();
    }
  else
    {
      /* Atomically restore the old signal mask
         (which had better not block SIGALRM),
         and wait for a signal to arrive.  */
#ifdef DEBUG
      debug_printf ("-- sleep: Waiting for the alarm\n");
#endif
      sigsuspend (&oset);
#ifdef DEBUG
      debug_printf ("-- sleep: Alarm has gone off. Continuing with execution\n");
#endif
      after = clock ();

      /* Restore the old signal action state.  */
      sigaction (SIGALRM, &oact, (struct sigaction *) NULL);
    }

  /* Notice how long we actually slept.  */
  slept = after - before;

  /* Restore the user's alarm if we have not already past it.
     If we have, be sure to turn off the alarm in case a signal
     other than SIGALRM was what woke us up.  */
  (void) ualarm (remaining > slept ? (useconds_t)(remaining - slept) * USECS_PER_CLOCK : 0, 0);

  /* Restore the original signal mask.  */
  (void) sigprocmask (SIG_SETMASK, &oset, (sigset_t *) NULL);

  /* Restore the `errno' value we started with.
     Some of the calls we made might have failed, but we don't care.  */
  (void) __set_errno (save);

  return slept > clockticks ? 0 : clockticks - slept;
}

/* 2026: sleep_int for any length, in chunks ualarm can express.  Returns
   the ticks not slept if a signal ended the sleep early.  */
static uint64_t
sleep_ticks (uint64_t clockticks)
{
  while (clockticks > (uint64_t) SLEEP_CHUNK)
    {
      clock_t left = sleep_int (SLEEP_CHUNK);
      clockticks -= (uint64_t) SLEEP_CHUNK;
      if (left > 0)
	return clockticks + (uint64_t) left;
    }
  return (uint64_t) sleep_int ((clock_t) clockticks);
}

/* Make the process sleep for SECONDS seconds, or until a signal arrives
   and is not ignored.  The function returns the number of seconds less
   than SECONDS which it actually slept (zero if it slept the full time).  */
unsigned int
sleep (unsigned int seconds)
{
  PTHREAD_SAFE_CANCELLATION

  return (unsigned int) (sleep_ticks ((uint64_t) seconds * CLOCKS_PER_SEC)
			 / CLOCKS_PER_SEC);
}


/* Sleep for time periods specified in micro-seconds.  */
int usleep (useconds_t usec)
{
  PTHREAD_SAFE_CANCELLATION

  /* An allowed & specified limitation. Otherwise our calculations might
     overflow.  */
  if (usec >= 1000000)
    return __set_errno (EINVAL);

  return (int) sleep_int ((usec + USECS_PER_CLOCK-1) / USECS_PER_CLOCK) * USECS_PER_CLOCK;
}

/* Sleep for time periods specified in nanoseconds.  */
extern uint64_t __ul_monotonic_ns (void);

int nanosleep (const struct timespec *req, struct timespec *rem)
{
  uint64_t target, want;
  PTHREAD_SAFE_CANCELLATION;

  if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec > 999999999)
    return __set_errno (EINVAL);

  /* 2026: sleep with sub-centisecond accuracy.  Whole centiseconds are
     slept as before (in chunks, so long sleeps don't overflow); the last
     10-20 ms are waited out against the high resolution monotonic clock,
     yielding to other threads when that's allowed.  If a sleep ends
     early without a signal, sleep again rather than spin.  */
  want = (uint64_t) req->tv_sec * 1000000000u + (uint64_t) req->tv_nsec;
  target = __ul_monotonic_ns () + want;
  for (;;)
    {
      uint64_t now = __ul_monotonic_ns ();
      uint64_t left;

      if (now >= target)
	break;
      left = target - now;
      if (left < 20000000u)
	{
	  safe_yield ();
	  continue;
	}
      if (sleep_ticks (left / NSECS_PER_CLOCK - 1) > 0)
	{
	  /* Interrupted by a signal.  */
	  if (rem != NULL)
	    {
	      now = __ul_monotonic_ns ();
	      left = now < target ? target - now : 0;
	      rem->tv_sec = (time_t) (left / 1000000000u);
	      rem->tv_nsec = (long) (left % 1000000000u);
	    }
	  return __set_errno (EINTR);
	}
    }

  if (rem != NULL)
    {
      rem->tv_sec = 0;
      rem->tv_nsec = 0;
    }
  return 0;
}
