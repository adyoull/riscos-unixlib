/* Internal UnixLib pthread.h
 * Written by Alex Waugh.
 * Copyright (c) 2002-2019 UnixLib Developers.
 */

#ifdef __TARGET_SCL__
#  error "SCL has no pthread support"
#endif

#ifndef __PTHREAD_H
#include_next <pthread.h>
#endif

#if !defined(__INTERNAL_PTHREAD_H) && defined(__PTHREAD_H)
#define __INTERNAL_PTHREAD_H

#include <kernel.h>

#define PTHREAD_DEFAULT_MAX_STACK_SIZE (1 * 1024 * 1024)

#if __UNIXLIB_CHUNKED_STACK
typedef struct __stack_chunk *stack_t;
#else
typedef void *stack_t;
#endif

/* Hold all details about the thread. A pthread_t points to one of these structures */
struct __pthread_thread
{
  int magic; /* Magic number */

  /* Space for registers on the context switch */
  struct __pthread_saved_context *saved_context;

  void *alloca; /* Storage for alloca_chunk linked list */

  int thread_errno; /* Value of errno for this thread */

  _kernel_oserror errbuf; /* Last RISC OS error from error handler */

  char errbuf_valid; /* Non-zero when errbuf field is valid.  */

  /* WARNING: Various assembly files refer to this structure, using
     offsets defined in internal/asm_dec.s.
     If you change the ordering of any of the elements above this
     comment then make sure the offsets are kept in sync. */

  struct __pthread_thread *next; /* Linked list of all threads */
  enum __pthread_state state; /* Running/blocked/idle/etc. */
  void *ret; /* Value returned from thread */

  void *(*start_routine)(void *); /* Function to call as the new thread */
  void *arg; /* Argument to pass to the start_routine */

  /* Initial stack chunk allocated to thread */
  stack_t stack;
  size_t stack_size;

  /* Thread that wishes to join with this thread */
  struct __pthread_thread *joined;

  /* Linked list of keys associated with this thread */
  struct __pthread_key *keys;

  /* Mutex that the thread is waiting for */
  pthread_mutex_t *mutex;

  /* Type of mutex that the thread is waiting for */
  enum __pthread_locktype mutextype;

  /* Condition var that the thread is waiting for */
  pthread_cond_t *cond;

  /* Timeout value for condition var */
  clock_t condtimeout;

  /* Next thread that is waiting on the same mutex/condition var */
  struct __pthread_thread *nextwait;

  /* Linked list of cleanup functions to call when this thread exits */
  struct __pthread_cleanup *cleanupfns;

  /* Scheduling parameters. Currently ignored */
  struct sched_param __param;

  /* Scheduling policy.  Currently ignored.  */
  int __policy;

  /* Cancelability state of this thread (enabled or disabled) */
  unsigned int cancelstate : 1;

  /* Cancelability type (asynchronous or deferred) */
  unsigned int canceltype : 1;

  /* Should this thread be cancelled when it next reaches a
     cancellation point.  */
  unsigned int cancelpending : 1;

  /* Is the thread detached of can it still be joined to.  */
  unsigned int detachstate : 1;

  /* Is the thread suspended.  */
  unsigned int suspended : 1;

  __sigset_t blocked; /* Signal mask for this thread. */
  __sigset_t pending; /* Pending signals for this thread */
  pthread_cond_t sigwait_cond;

  /* Human readable name of thread.  */
  char name[PTHREAD_MAX_NAMELEN_NP];
};

extern pthread_t __pthread_running_thread; /* Currently running thread */

/* Print lots of general debugging info */
/*#define PTHREAD_DEBUG*/
/* Print debug info for the context switcher */
/*#define PTHREAD_DEBUG_CONTEXT*/

/* Magic number to check a pthread_t is valid */
#define PTHREAD_MAGIC 0x52485450

#define __pthread_invalid(thread) \
	((thread) == NULL || (thread)->magic != PTHREAD_MAGIC)

/* Holds data about a thread specific key */
struct __pthread_key
{
  pthread_key_t keyid; /* Key that this value is attached to */
  union {
    const void *constvalue;
    void *nonconst;
  } value; /* Value of the key for this thread. Mess around with a union
              to get around const issues */
  void (*destructor) (void*); /* Destructor function to call for this value */
  struct __pthread_key *next; /* Linked list of all keys for this thread */
};

/* Holds all the state associated with a thread when it is switched out */
struct __pthread_saved_context
{
  int r[16]; /* User mode integer registers */
  int spsr;
#ifndef __SOFTFP__
#  ifdef __VFP_FP__
  int vfpcontext; /* VFPSupport context ptr. If bit 0 set, was stack allocated, else malloc'd */
#  else
  char fpregs[12*8]; /* Floating point registers */
  int fpstatus; /* Floating point status register */
#  endif
#endif
};

/* A pointer to this structure (allocated from RMA) is passed to
   pthread_call_every in r12 so that it can validate the application
   without accessing its address space.
   As the filter name also needs to be RMA, it's convenient to use
   the same block.  */
struct __pthread_callevery_block
{
 /* User registers are preserved in here for the callback execution.
  * User registers = R0-R15 and CPSR in 32-bit mode
  *		   = R0-R15 only in 26-bit mode (the 17th word equals
  *		 	    the pc value)  */
  int callback_regs[17];
  /* bit 0 Escape condition flag
   * bit 1 Internet event flag */
  unsigned callback_flag;
  /* Signal number to use for __unixlib_raise_signal().  */
  int callback_a1;
  void *sul_upcall_addr;
  void *sul_upcall_r12;
  void *got_ptr;		/* Unixlib Global Offset Table ptr.  */
  /* Have we registered the CallEvery ticker ?
     Value 0 : no, CallEvery ticker is not enabled
	   1 : yes CallEvery ticker is enabled  */
  unsigned ticker_started;
  volatile int pthread_worksemaphore; /* Zero if the context switcher is
    allowed to switch threads.  */
  volatile int pthread_callback_semaphore; /* Prevent a callback being set
    whilst servicing another callback.  */
  char filter_name[20];
  /* 2026: counters, written by the ticker routines, reported at exit when
     UnixLib$TickerStats is set (docs/THREAD-TICKER.md).  */
  unsigned ticks;		/* Ticker calls.  */
  unsigned foreign_ticks;	/* ... that found another task paged in.  */
  void *foreign_handler;	/* Upcall handler and R12 seen at the last  */
  void *foreign_r12;		/* foreign tick.  */
  unsigned pre_calls;		/* Wimp pre-filter calls.  */
  unsigned post_calls;		/* Wimp post-filter calls.  */
  int filter_handle;		/* Task the filters are registered for.  */
  unsigned flags;		/* Bit 0: the PThreadTicker module's routines.  */
  /* 2026 (5.0.3.1): the ticker keeps running while the program is in
     Wimp_Poll; a tick then only sets 'pending', and the post-filter
     switches threads as Wimp_Poll returns (docs/THREAD-TICKER.md).  */
  volatile unsigned polling;	/* Between the pre- and post-filter.  */
  volatile unsigned pending;	/* A tick came while polling.  */
  unsigned post_switches;	/* Switches started by the post-filter.  */
  /* A copy of the ticker routines, used when the PThreadTicker module
     isn't loaded.  OS_CallEvery runs the handler from
     here: the ticker can fire while another task is paged in, when the
     application's own copy isn't there.  */
  unsigned ticker_code[119];
};

/* pthread/ticker.c (2026).  */
extern void __pthread_ticker_read_task (int *__handle, int *__version);
extern void __pthread_ticker_init (void);
extern void __pthread_ticker_recheck (void);
extern void __pthread_ticker_fini (void);
extern int __pthread_ticker_owner (void);
extern void __pthread_ticker_write_stats (void);

/* The ticker routines in _context.s (internal/ticker.s), copied into
   ticker_code at start-up when SharedUnixLibrary can't run them.  */
extern const char __pthread_call_every_code[], __pthread_call_every_code_end[];

extern pthread_t __pthread_thread_list; /* Linked list of all threads */

/* Called once early in program initialisation */
extern void __pthread_prog_init (void);

extern void __pthread_prog_fini (void);

/* Called if a non recoverable error is detected */
extern void __pthread_fatal_error (const char *msg);

/* Initialise a new node, allocating it if node is NULL */
extern pthread_t __pthread_new_node (pthread_t node);

/* Common routines used by mutex/rwlock/cond functions */
extern int __pthread_lock_lock (pthread_mutex_t *mutex, const enum __pthread_locktype, const int trylock);
extern int __pthread_lock_unlock (pthread_mutex_t *mutex, const int yield);

/* Call all cleanup handlers registered for the current thread */
extern void __pthread_cleanup_callhandlers (void);

/* Call all destructors for remaining thread specific key values */
extern void __pthread_key_calldestructors (void);

/* Called by fork() just before the fork is about to happen */
extern void __pthread_atfork_callprepare (void);
/* Called by fork() once the fork has happened */
extern void __pthread_atfork_callparentchild (const int parent);

/* Allocate and initialise a stack chunk for a new thread */
extern stack_t __pthread_new_stack (pthread_t node);

/* Main scheduling code */
extern void __pthread_context_switch (void);

/* Assembly functions */

/* Prevent the callevery interrupt from initialising a context switch.  */
extern int __pthread_disable_ints (void);
/* Allow the callevery interrupt from initialising a context switch.  */
extern int __pthread_enable_ints (void);
/* Initialise a context save area.  */
extern void __pthread_init_save_area (struct __pthread_saved_context *);
/* Calls alloca cleanup functions on thread exit.  */
extern void __pthread_exit (void);

/* Prevent the callevery interrupt from initialising a context switch,
   and register a function to reenable them when the current function
   returns.  */
extern void __pthread_protect_unsafe (void);

/* Should be placed at the beginning of a function body for a
   thread safe function that is defined as a cancellation point.  */
#define PTHREAD_SAFE_CANCELLATION \
  do \
    { \
      if (__ul_global.pthread_system_running) \
        pthread_testcancel (); \
    } while (0);

/* Should be placed at the beginning of a function body for a
   function that is not reentrant.  */
#define PTHREAD_UNSAFE \
  do \
    { \
      if (__ul_global.pthread_system_running) \
        __pthread_protect_unsafe (); \
    } while (0);

/* Should be placed at the beginning of a function body for a
   function that is not reentrant and is also defined as a cancellation
   point.  */
#define PTHREAD_UNSAFE_CANCELLATION \
  do \
    { \
      if (__ul_global.pthread_system_running) \
        { \
          pthread_testcancel (); \
          __pthread_protect_unsafe (); \
        } \
    } while (0);

__END_DECLS

#endif
