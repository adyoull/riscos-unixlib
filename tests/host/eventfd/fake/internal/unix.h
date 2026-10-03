/* Host test shim.  */
struct __sul_process
{
  int pid;
  int fdhandlesize;
  void *(*sul_malloc) (int, int);
  void (*sul_free) (int, void *);
};
struct ul_global
{
  struct __sul_process *sulproc;
  int pthread_system_running;
};
extern struct ul_global __ul_global;
extern int __set_errno (int);
