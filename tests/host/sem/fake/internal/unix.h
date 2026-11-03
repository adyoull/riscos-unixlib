/* Host test shim.  */
#include <errno.h>
struct __pthread_callevery_block { volatile int pthread_worksemaphore; };
struct ul_global
{
  int pthread_system_running;
  int executing_signalhandler;
  struct __pthread_callevery_block *pthread_callevery_rma;
};
extern struct ul_global __ul_global;
static inline int __set_errno (int val) { errno = val; return -1; }
