/* sched_get_priority_min (), sched_get_priority_max ()
   Copyright (c) 2026 UnixLib Developers.  */

#include <errno.h>
#include <sched.h>

/* 2026: UnixLib's thread scheduler is a plain round robin that ignores
   priorities, so every policy has the single priority 0.  Programs that
   ask (OpenAL Soft, SDL-style thread code) get a valid, empty range
   instead of a link error.  */

static int
known_policy (int policy)
{
  return policy == SCHED_OTHER || policy == SCHED_FIFO
	 || policy == SCHED_RR || policy == SCHED_SPORADIC;
}

int
sched_get_priority_min (int policy)
{
  if (!known_policy (policy))
    return __set_errno (EINVAL);
  return 0;
}

int
sched_get_priority_max (int policy)
{
  if (!known_policy (policy))
    return __set_errno (EINVAL);
  return 0;
}
