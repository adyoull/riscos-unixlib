/* Host test shim for pthread/sem.c: just what it and semaphore.h use, with
   the field names of UnixLib's own thread structure.  */
#ifndef FAKE_PTHREAD_H
#define FAKE_PTHREAD_H
#include <time.h>
enum __pthread_state
{
  STATE_IDLE, STATE_MUTEX_WAIT, STATE_COND_WAIT, STATE_COND_TIMED_WAIT,
  STATE_JOIN, STATE_UNALLOCED, STATE_BLOCKED, STATE_RUNNING
};
struct __pthread_thread;
typedef struct __pthread_thread *pthread_t;
typedef struct __pthread_cond
{
  struct __pthread_thread *waiting;
  clockid_t clock_id;
} pthread_cond_t;
typedef struct { int dummy[4]; } pthread_rwlock_t;
struct __pthread_thread
{
  enum __pthread_state state;
  pthread_cond_t *cond;
  clock_t condtimeout;
  struct __pthread_thread *nextwait;
};
extern pthread_t __pthread_running_thread;
extern int __pthread_disable_ints (void);
extern int __pthread_enable_ints (void);
extern void pthread_yield (void);
extern void pthread_testcancel (void);
extern int pthread_rwlock_init (pthread_rwlock_t *, const void *);
#define PTHREAD_SAFE_CANCELLATION \
  do { if (__ul_global.pthread_system_running) pthread_testcancel (); } while (0);
#endif
