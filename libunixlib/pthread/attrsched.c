/* Thread scope, scheduling inheritance and policy attributes.
   Copyright (c) 2026 UnixLib Developers.  */

#include <pthread.h>
#include <sched.h>
#include <errno.h>

/* 2026: contention scope, scheduling inheritance and policy. UnixLib's
   threads have one scope and no scheduling policies, and pthread_attr_t
   has no room for these (its layout is public), so the set functions
   check the value and change nothing, and the get functions give what
   UnixLib does. Requested by riscos-reeledit (MLT).

   In a file of their own, so that a program that defines any of these
   itself still links with the static library.  */
int
pthread_attr_setscope (pthread_attr_t *attr, int scope)
{
  if (attr == NULL
      || (scope != PTHREAD_SCOPE_SYSTEM && scope != PTHREAD_SCOPE_PROCESS))
    return EINVAL;
  /* Valid, but not what UnixLib's threads have (glibc and musl say the
     same).  */
  if (scope == PTHREAD_SCOPE_PROCESS)
    return ENOTSUP;
  return 0;
}

int
pthread_attr_getscope (const pthread_attr_t *attr, int *scope)
{
  if (attr == NULL || scope == NULL)
    return EINVAL;
  *scope = PTHREAD_SCOPE_SYSTEM;
  return 0;
}

int
pthread_attr_setinheritsched (pthread_attr_t *attr, int inherit)
{
  if (attr == NULL
      || (inherit != PTHREAD_INHERIT_SCHED && inherit != PTHREAD_EXPLICIT_SCHED))
    return EINVAL;
  return 0;
}

int
pthread_attr_getinheritsched (const pthread_attr_t *attr, int *inherit)
{
  if (attr == NULL || inherit == NULL)
    return EINVAL;
  *inherit = PTHREAD_INHERIT_SCHED;
  return 0;
}

int
pthread_attr_setschedpolicy (pthread_attr_t *attr, int policy)
{
  if (attr == NULL
      || (policy != SCHED_OTHER && policy != SCHED_FIFO && policy != SCHED_RR))
    return EINVAL;
  return 0;
}

int
pthread_attr_getschedpolicy (const pthread_attr_t *attr, int *policy)
{
  if (attr == NULL || policy == NULL)
    return EINVAL;
  *policy = SCHED_OTHER;
  return 0;
}
