/* Just the __ul_global fields pthread/ticker.c uses.  */
struct ul_global
{
  int taskhandle;
  int pthread_system_running;
  int pthread_num_running_threads;
  struct __pthread_callevery_block *pthread_callevery_rma;
};
extern struct ul_global __ul_global;
