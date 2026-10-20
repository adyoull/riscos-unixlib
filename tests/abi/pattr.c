/* <pthread.h>: contention scope, scheduling inheritance and policy
   (2026), as MLT's mlt_consumer.c uses them. Must compile with implicit
   declarations as errors, and link.  */
#include <pthread.h>
#include <sched.h>

_Static_assert (PTHREAD_SCOPE_SYSTEM != PTHREAD_SCOPE_PROCESS, "scope");
_Static_assert (PTHREAD_INHERIT_SCHED != PTHREAD_EXPLICIT_SCHED, "inherit");

int
main (void)
{
  pthread_attr_t a;
  struct sched_param p = { 0 };
  int scope, inherit, policy;

  pthread_attr_init (&a);
  if (pthread_attr_setscope (&a, PTHREAD_SCOPE_SYSTEM)
      || pthread_attr_setinheritsched (&a, PTHREAD_EXPLICIT_SCHED)
      || pthread_attr_setschedpolicy (&a, SCHED_OTHER)
      || pthread_attr_setschedparam (&a, &p))
    return 1;
  if (pthread_attr_getscope (&a, &scope)
      || pthread_attr_getinheritsched (&a, &inherit)
      || pthread_attr_getschedpolicy (&a, &policy))
    return 2;
  return pthread_attr_setscope (&a, 99) == 0;	/* must be EINVAL */
}
