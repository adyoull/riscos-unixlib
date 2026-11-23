/* Get time.
   Copyright (c) 2003-2013 UnixLib Developers.  */

#include <errno.h>
#include <kernel.h>
#include <time.h>
#include <sys/time.h>

#include <internal/os.h>
#include <internal/local.h>

extern int __ul_realtime_now (unsigned int *sec, unsigned int *nsec);

/* The `gettimeofday' function returns the current date and time in
   the `struct timeval' structure indicated by TP.  Information about
   the time zone is returned in the structure pointed at TZP.  If the
   TZP argument is a NULL pointer, time zone information is ignored.

   The return value is `0' on success and `-1' on failure.  The
   following `errno' error condition is defined for this function:

   `ENOSYS'
   The operating system does not support getting time zone
   information, and TZP is not a null pointer.  The GNU
   operating system does not support using `struct timezone' to
   represent time zone information; that is an obsolete feature
   of 4.3 BSD.  */


/* Get the current time of day and timezone information,
   putting it into *TV and *TZ.  If TZ is NULL, *TZ is not filled.
   Returns 0 on success, -1 on errors.  */
int
gettimeofday (struct timeval *tv, struct timezone *tz)
{
  if (tv == NULL)
    return __set_errno (EINVAL);

  unsigned int sec, nsec;
  if (__ul_realtime_now (&sec, &nsec) < 0)	/* T7: time/clk_gettime.c */
    return -1;
  tv->tv_sec = (time_t) sec;
  tv->tv_usec = (suseconds_t) (nsec / 1000u);

  if (tz != NULL)
    {
      tzset (); /* Initialises timezone & daylight.  */

      tz->tz_minuteswest = timezone / 60;
      tz->tz_dsttime = 0;
    }

  return 0;
}
