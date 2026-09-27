/* Control thread scheduling parameters.
   Copyright (c) 2005, 2006 UnixLib Developers.  */

#include <errno.h>
#include <pthread.h>
#include <sched.h>

/* Set scheduling parameter.
   2026: the policy test used || and so rejected every policy with EINVAL.
   UnixLib's scheduler is a round robin without priorities: SCHED_OTHER
   with priority 0 is accepted (and recorded), the real-time policies are
   refused with ENOTSUP as POSIX allows, and anything else is EINVAL.  */
int pthread_setschedparam (pthread_t thread, int policy,
			   const struct sched_param *param)
{
  if (thread == NULL || param == NULL)
    return EINVAL;

  if (policy == SCHED_FIFO || policy == SCHED_RR || policy == SCHED_SPORADIC)
    return ENOTSUP;
  if (policy != SCHED_OTHER
      || param->sched_priority < sched_get_priority_min (policy)
      || param->sched_priority > sched_get_priority_max (policy))
    return EINVAL;

  thread->__policy = policy;
  thread->__param = *param;
  return 0;
}

/* Get scheduling parameter. */
int pthread_getschedparam (pthread_t thread,
			   int *policy,
			   struct sched_param *param)
{
  if (thread == NULL || policy == NULL || param == NULL)
    return EINVAL;

  *policy = thread->__policy;
  *param = thread->__param;
  return 0;
}
