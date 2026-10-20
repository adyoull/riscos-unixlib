/* A program with its own pthread_attr_setinheritsched (as old ports'
   stubs may have) must still link with the static library: the 2026
   scope/inherit/policy functions are in their own object, not
   pthread_attr_init's.  */
#include <pthread.h>

int
pthread_attr_setinheritsched (pthread_attr_t *a, int i)
{
  (void) a;
  (void) i;
  return 0;
}

int
main (void)
{
  pthread_attr_t a;

  pthread_attr_init (&a);
  return pthread_attr_setinheritsched (&a, PTHREAD_EXPLICIT_SCHED);
}
