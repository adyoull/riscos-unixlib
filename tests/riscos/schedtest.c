/* schedtest: sched_get_priority_min/max and pthread_setschedparam.
   Before: the sched_get_priority_* functions didn't exist (OpenAL Soft
   failed to link) and pthread_setschedparam refused every policy. */
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

static int bad;
static void check (const char *what, int ok)
{
  printf ("%-52s %s\n", what, ok ? "ok" : "FAIL");
  bad |= !ok;
}

int main (void)
{
  struct sched_param sp;
  int policy = -1, r;

  check ("sched_get_priority_min(SCHED_OTHER) == 0", sched_get_priority_min (SCHED_OTHER) == 0);
  check ("sched_get_priority_max(SCHED_RR) == 0", sched_get_priority_max (SCHED_RR) == 0);
  errno = 0;
  check ("sched_get_priority_min(99) == -1, EINVAL",
	 sched_get_priority_min (99) == -1 && errno == EINVAL);

  r = pthread_getschedparam (pthread_self (), &policy, &sp);
  check ("new thread: policy SCHED_OTHER, priority 0",
	 r == 0 && policy == SCHED_OTHER && sp.sched_priority == 0);

  sp.sched_priority = 0;
  check ("setschedparam(SCHED_OTHER, 0) == 0",
	 pthread_setschedparam (pthread_self (), SCHED_OTHER, &sp) == 0);
  sp.sched_priority = sched_get_priority_min (SCHED_RR);
  check ("setschedparam(SCHED_RR, min) == ENOTSUP (as OpenAL asks)",
	 pthread_setschedparam (pthread_self (), SCHED_RR, &sp) == ENOTSUP);
  sp.sched_priority = 5;
  check ("setschedparam(SCHED_OTHER, 5) == EINVAL",
	 pthread_setschedparam (pthread_self (), SCHED_OTHER, &sp) == EINVAL);

  printf ("%s\n", bad ? "FAIL" : "PASS");
  return bad;
}
