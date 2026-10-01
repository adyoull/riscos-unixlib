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
   Falls back to centisecond resolution if the HAL values look wrong.  */
#include <stdint.h>
#include <swis.h>

#ifndef __TARGET_SCL__
#  include <pthread.h>
#  include <internal/unix.h>
/* 2026: hr_last_ns is 64 bits, so reading or updating it takes two
   instructions; hold off thread switches so a thread can't see half an
   update (which could make the clock jump ahead and stick there).  */
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

static int hr_state;			/* 0 = unknown, 1 = usable, -1 = not */
static unsigned int hr_rate, hr_period;
static uint64_t hr_last_ns;

static int
hr_init (void)
{
  unsigned int rate, period;

  if (hr_state != 0)
    return hr_state > 0;

  hr_state = -1;
  if (_swix (OS_Hardware, _INR(8,9)|_OUT(0), 0, HAL_CounterRate, &rate) != NULL
      || _swix (OS_Hardware, _INR(8,9)|_OUT(0), 0, HAL_CounterPeriod, &period) != NULL)
    return 0;

  /* The counter must reload once per centisecond.  */
  if (rate < 10000 || period == 0
      || period < rate / 100 - rate / 10000 - 1
      || period > rate / 100 + rate / 10000 + 1)
    return 0;

  hr_rate = rate;
  hr_period = period;
  hr_state = 1;
  return 1;
}

uint64_t
__ul_monotonic_ns (void)
{
  unsigned int cs1, cs2, cnt = 0;
  uint64_t ns;

  if (!hr_init ())
    return (uint64_t) clock () * 10000000u;

  do
    {
      _swix (OS_ReadMonotonicTime, _OUT(0), &cs1);
      if (_swix (OS_Hardware, _INR(8,9)|_OUT(0), 0, HAL_CounterRead, &cnt) != NULL)
	{
	  hr_state = -1;
	  return (uint64_t) cs1 * 10000000u;
	}
      _swix (OS_ReadMonotonicTime, _OUT(0), &cs2);
    }
  while (cs1 != cs2);

  if (cnt > hr_period)
    cnt = hr_period;
  ns = (uint64_t) cs1 * 10000000u
       + (uint64_t) (hr_period - cnt) * 1000000000u / hr_rate;

  /* The counter can reload a moment before the centisecond count is
     incremented; never let the clock go backwards.  */
  HR_LOCK ();
  if (ns < hr_last_ns)
    ns = hr_last_ns;
  hr_last_ns = ns;
  HR_UNLOCK ();
  return ns;
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
	  uint64_t ns = __ul_monotonic_ns ();
	  tp->tv_sec = (time_t) (ns / 1000000000u);
	  tp->tv_nsec = (long) (ns % 1000000000u);
	  break;
	}

      default:
        return __set_errno (EINVAL);
    }

  return 0;
}
