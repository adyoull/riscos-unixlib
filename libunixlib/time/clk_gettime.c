/* Read clock information.
   Copyright (c) 2005-2013 UnixLib Developers.  */

#include <errno.h>
#include <kernel.h>
#include <time.h>

#include <internal/os.h>
#include <internal/local.h>

/* 2026: high resolution monotonic time.  RISC OS's monotonic timer only
   ticks every centisecond, which makes frame pacing in games (via
   std::chrono::steady_clock / SDL) jitter by up to 10ms.  Interpolate
   within the current centisecond using the HAL counter (timer 0, which
   generates the centisecond tick).  HAL entries: 19 = HAL_CounterRate,
   20 = HAL_CounterPeriod, 21 = HAL_CounterRead (all read-only).
   Falls back to centisecond resolution if the HAL values look wrong.
   (T6, below: Linux's clock under RISC OS on Linux, and no divisions.)  */
#include <stdint.h>
#include <swis.h>

#ifndef __TARGET_SCL__
#  include <pthread.h>
#  include <internal/unix.h>
/* 2026: the last time given (hr_last_sec, hr_last_nsec) is two words,
   so reading or updating it takes two instructions; hold off thread
   switches so a thread can't see half an update (which could make the
   clock jump ahead and stick there).  */
#  define HR_LOCK() \
  do { if (__ul_global.pthread_system_running) __pthread_disable_ints (); } while (0)
#  define HR_UNLOCK() \
  do { if (__ul_global.pthread_system_running) __pthread_enable_ints (); } while (0)
#else
#  define HR_LOCK() do { } while (0)
#  define HR_UNLOCK() do { } while (0)
#endif

#define HAL_CounterRate   19
#define HAL_CounterPeriod 20
#define HAL_CounterRead   21

/* 2026 (T6, from riscos-unixlib-web W32): cheaper.  A browser's main
   loops read the monotonic clock thousands of times a second (GLib asks
   before and after every poll); profiled on a Pi 4 under RISC OS on Linux, reading it
   was 13% of a busy task's time.  Each read was three SWIs (a SWI costs
   about 4.5 us there) and three 64-bit divisions.  Now:

   - On RISC OS on Linux, one call to Linux's own clock_gettime
     (CLOCK_MONOTONIC, through IXSupport_LinuxSyscall), which has
     nanosecond resolution: no interpolation needed.  Tried once; on
     native RISC OS IXSupport isn't there and the HAL way is used.
   - The HAL way: the same three SWIs, but no division: the fraction of
     the centisecond is a multiply by a factor worked out once, and
     seconds and nanoseconds come from the centisecond count, which fits
     in 32 bits.

   Whichever is chosen is kept for the program's life (they count from
   different starting points).  */
#define IXSupport_LinuxSyscall 0x59EC0
#define LINUX_clock_gettime 263		/* ARM EABI, 32-bit time_t */
#define LINUX_clock_gettime64 403	/* the same with 64-bit time (kernels without 32-bit time calls) */
#define LINUX_CLOCK_MONOTONIC 1

static int hr_state;			/* 0 = unknown, 1 = HAL, 2 = Linux, -1 = centiseconds */
static unsigned int hr_period;
static uint64_t hr_scale;		/* ns per counter tick, << 16 */
static unsigned int hr_last_sec, hr_last_nsec;

static int linux_call = LINUX_clock_gettime;

static int
linux_now (unsigned int *sec, unsigned int *nsec)
{
  union { struct { int32_t tv_sec, tv_nsec; } t32;
	  struct { int64_t tv_sec, tv_nsec; } t64; } ts;
  int result;
  /* IXSupport_LinuxSyscall: r7 the call's number, r0-r6 its arguments,
     r0 its result (negative: -errno).  */
  if (_swix (IXSupport_LinuxSyscall, _INR(0,1)|_IN(7)|_OUT(0),
	     LINUX_CLOCK_MONOTONIC, &ts, linux_call, &result) != NULL)
    return 0;
  if (result == -38 /* ENOSYS */ && linux_call == LINUX_clock_gettime)
    {
      linux_call = LINUX_clock_gettime64;
      return linux_now (sec, nsec);
    }
  if (result != 0)
    return 0;
  if (linux_call == LINUX_clock_gettime64)
    {
      if (ts.t64.tv_sec < 0 || ts.t64.tv_sec > 0xFFFFFFFFll
	  || (uint64_t) ts.t64.tv_nsec >= 1000000000u)
	return 0;
      *sec = (unsigned int) ts.t64.tv_sec;
      *nsec = (unsigned int) ts.t64.tv_nsec;
      return 1;
    }
  if (ts.t32.tv_sec < 0 || (uint32_t) ts.t32.tv_nsec >= 1000000000u)
    return 0;
  *sec = (unsigned int) ts.t32.tv_sec;
  *nsec = (unsigned int) ts.t32.tv_nsec;
  return 1;
}

static int
hr_init (void)
{
  unsigned int rate, period, sec, nsec;

  if (hr_state != 0)
    return hr_state;

  if (linux_now (&sec, &nsec))
    return hr_state = 2;

  hr_state = -1;
  if (_swix (OS_Hardware, _INR(8,9)|_OUT(0), 0, HAL_CounterRate, &rate) != NULL
      || _swix (OS_Hardware, _INR(8,9)|_OUT(0), 0, HAL_CounterPeriod, &period) != NULL)
    return hr_state;

  /* The counter must reload once per centisecond.  */
  if (rate < 10000 || period == 0
      || period < rate / 100 - rate / 10000 - 1
      || period > rate / 100 + rate / 10000 + 1)
    return hr_state;

  hr_period = period;
  /* ticks * hr_scale >> 16 = ns: 10^7 ns in a period of ticks (the
     counter's own period, so a whole period is exactly 1 cs). ticks is at
     most the period, so the product is at most 10^7 << 16, < 2^40.  */
  hr_scale = (10000000ull << 16) / period;
  return hr_state = 1;
}

/* The monotonic time as seconds and nanoseconds, never going backwards.  */
static void
monotonic_now (unsigned int *sec_out, unsigned int *nsec_out)
{
  unsigned int cs1, cs2, cnt = 0, sec, nsec;

  switch (hr_init ())
    {
    case 2:
      if (linux_now (&sec, &nsec))
	break;
      /* It worked before, so this shouldn't happen.  Don't fall back to
	 clock (), which counts from RISC OS's start, not Linux's: the
	 clock would then stop until clock () caught up (review,
	 2026-11-25).  Give the last time again and try Linux next time.  */
      HR_LOCK ();
      sec = hr_last_sec;
      nsec = hr_last_nsec;
      HR_UNLOCK ();
      break;
    case -1:
      cs1 = (unsigned int) clock ();
      sec = cs1 / 100;
      nsec = (cs1 % 100) * 10000000u;
      break;
    default:
      do
	{
	  _swix (OS_ReadMonotonicTime, _OUT(0), &cs1);
	  if (_swix (OS_Hardware, _INR(8,9)|_OUT(0), 0, HAL_CounterRead, &cnt) != NULL)
	    {
	      hr_state = -1;
	      cs2 = cs1;
	      cnt = hr_period;
	      break;
	    }
	  _swix (OS_ReadMonotonicTime, _OUT(0), &cs2);
	}
      while (cs1 != cs2);
      if (cnt > hr_period)
	cnt = hr_period;
      sec = cs1 / 100;
      nsec = (cs1 % 100) * 10000000u
	     + (unsigned int) (((uint64_t) (hr_period - cnt) * hr_scale) >> 16);
      if (nsec >= 1000000000u)		/* (a whole period: 1 cs) */
	{
	  nsec -= 1000000000u;
	  sec++;
	}
      break;
    }

  /* The counter can reload a moment before the centisecond count is
     incremented; never let the clock go backwards.  */
  HR_LOCK ();
  if (sec < hr_last_sec || (sec == hr_last_sec && nsec < hr_last_nsec))
    {
      sec = hr_last_sec;
      nsec = hr_last_nsec;
    }
  hr_last_sec = sec;
  hr_last_nsec = nsec;
  HR_UNLOCK ();
  *sec_out = sec;
  *nsec_out = nsec;
}

uint64_t
__ul_monotonic_ns (void)
{
  unsigned int sec, nsec;
  monotonic_now (&sec, &nsec);
  return (uint64_t) sec * 1000000000u + nsec;
}

int
clock_gettime (clockid_t clk_id, struct timespec *tp)
{
  if (tp == NULL)
    return __set_errno (EFAULT);

  switch (clk_id)
    {
      case CLOCK_REALTIME:
        {
	  unsigned int buf[2];
	  buf[0] = 3;
	  if (_kernel_osword (14, (int *)buf) < 0)
	    return -1;

	  /* Convert RISC OS time to Unix time (with csec resolution).  */
	  __int64_t csec = __cvt_riscos_time_csec (((__int64_t)(buf[1] & 0xFF) << 32)
						   + buf[0]);
	  tp->tv_sec = csec / 100;
	  tp->tv_nsec = csec % 100;
	  if (tp->tv_nsec < 0)
	    {
	      tp->tv_nsec = 100 + tp->tv_nsec;
	      tp->tv_sec -= 1;
	    }
	  tp->tv_nsec *= 10000000;
          break;
        }

      case CLOCK_MONOTONIC:
	{
	  unsigned int sec, nsec;
	  monotonic_now (&sec, &nsec);
	  tp->tv_sec = (time_t) sec;
	  tp->tv_nsec = (long) nsec;
	  break;
	}

      default:
        return __set_errno (EINVAL);
    }

  return 0;
}
