/* Just what pthread/ticker.c needs from UnixLib's internal <pthread.h>.
   The field names must match incl-local/pthread.h (the layout doesn't
   matter here; pthinit.c checks it against asm_dec.s).  */
#ifndef FAKE_PTHREAD_H
#define FAKE_PTHREAD_H
struct __pthread_callevery_block
{
  int callback_regs[17];
  unsigned callback_flag;
  int callback_a1;
  void *sul_upcall_addr;
  void *sul_upcall_r12;
  void *got_ptr;
  unsigned ticker_started;
  volatile int pthread_worksemaphore;
  volatile int pthread_callback_semaphore;
  char filter_name[20];
  unsigned ticks, foreign_ticks;
  void *foreign_handler, *foreign_r12;
  unsigned pre_calls, post_calls;
  int filter_handle;
  unsigned flags;
  volatile unsigned polling, pending;
  unsigned post_switches;
  unsigned ticker_code[119];
};
extern const char __pthread_call_every_code[], __pthread_call_every_code_end[];
extern void __pthread_ticker_read_task (int *, int *);
extern void __pthread_ticker_init (void);
extern void __pthread_ticker_recheck (void);
extern void __pthread_ticker_fini (void);
extern int __pthread_ticker_owner (void);
extern void __pthread_ticker_write_stats (void);
extern void __pthread_start_ticker (void);
extern void __pthread_stop_ticker (void);
#endif
