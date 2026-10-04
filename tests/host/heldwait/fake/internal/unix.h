/* Host test shim.  */
struct __pthread_callevery_block { volatile int pthread_worksemaphore; };
struct ul_global
{
  int pthread_system_running;
  struct __pthread_callevery_block *pthread_callevery_rma;
  void *pthread_return_address;
};
extern struct ul_global __ul_global;
