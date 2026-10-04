/* Fake <internal/unix.h>: just what the sound code reads.  */
struct fake_callevery_block { int pthread_worksemaphore; };
struct ul_global
{
  int pthread_system_running;
  struct fake_callevery_block *pthread_callevery_rma;
  void *pthread_return_address;
};
extern struct ul_global __ul_global;
