/* Pthread initialisation.
   Written by Martin Piper and Alex Waugh.
   Copyright (c) 2002-2008, 2010, 2015, 2019 UnixLib Developers.  */

#include <stdlib.h>
#include <pthread.h>
#include <malloc.h>
#include <swis.h>
#include <string.h>

#include <internal/swiparams.h>
#include <internal/os.h>
#include <internal/unix.h>

static struct __pthread_thread mainthread;

/* 2026: the RMA block's layout is also in incl-local/internal/asm_dec.s
   (PTHREAD_CALLEVERY_RMA_*); the ticker code copy must be where the
   assembler expects it.  */
#define OFFSET(f) __builtin_offsetof (struct __pthread_callevery_block, f)
_Static_assert (OFFSET (ticks) == 120, "ticks: keep in step with asm_dec.s");
_Static_assert (OFFSET (flags) == 148, "flags: keep in step with asm_dec.s");
_Static_assert (OFFSET (polling) == 152, "polling: keep in step with asm_dec.s");
_Static_assert (OFFSET (post_switches) == 160,
		"post_switches: keep in step with asm_dec.s");
_Static_assert (OFFSET (ticker_code) == 164,
		"ticker_code: keep in step with asm_dec.s");
_Static_assert (sizeof (struct __pthread_callevery_block) == 640,
		"__pthread_callevery_block: keep in step with asm_dec.s");
#undef OFFSET

static const char filter_name[] = "UnixLib pthread";

/* The block size sys/_syslib.s claimed from the RMA (see there).  */
extern const unsigned int __pthread_callevery_block_size;

static void
__get_main_stack (struct __pthread_thread *thread)
{
#if __UNIXLIB_CHUNKED_STACK
  register unsigned int sl __asm__ ("r10");

  thread->stack = (stack_t)(sl - 536);
  thread->stack_size = PTHREAD_STACK_MIN;
#else
  void *fp = __builtin_frame_address (0);
  stack_t stack;
  unsigned stack_base;
  unsigned guard_size;

  _swix(ARMEABISupport_StackOp, _INR(0,1)|_OUT(1), ARMEABISUPPORT_STACKOP_GET_STACK, fp, &thread->stack);

  _swix(ARMEABISupport_StackOp, _INR(0,1)|_OUT(1), ARMEABISUPPORT_STACKOP_GET_BOUNDS, thread->stack, &stack_base);
  _swix(ARMEABISupport_StackOp, _INR(0,1)|_OUTR(1,2), ARMEABISUPPORT_STACKOP_GET_SIZE, thread->stack, &thread->stack_size, &guard_size);
#endif
}

/* Called once, at program initialisation.  */
void
__pthread_prog_init (void)
{
  struct ul_global *gbl = &__ul_global;
#ifdef PTHREAD_DEBUG
  debug_printf ("-- __pthread_prog_init: Program initialisation\n");
#endif

  /* Create a node for the main program.  Calling this function with a
     valid argument means that we must setup 'saved_context' ourselves.  */
  __pthread_running_thread = __pthread_new_node (&mainthread);
  if (__pthread_running_thread == NULL)
    __unixlib_fatal ("pthreads initialisation error: out of memory");

  mainthread.saved_context =
    malloc_unlocked (gbl->malloc_state,
		     sizeof (struct __pthread_saved_context));
  if (mainthread.saved_context == NULL)
    __unixlib_fatal ("pthreads initialisation error: out of memory");

#if !defined(__SOFTFP__) && defined(__VFP_FP__)
  /* Store the ID of the VFP context that was created earlier on */
  _swi(VFPSupport_ActiveContext, _OUT(0), &mainthread.saved_context->vfpcontext);
  /* Set the bottom bit to flag that it wasn't malloc'd */
  mainthread.saved_context->vfpcontext |= 1;
#endif

  mainthread.magic = PTHREAD_MAGIC;
  __get_main_stack (&mainthread);

  /* 2026: _syslib.s claims the RMA block with the assembler's idea of its
     size.  An incremental build once kept an old _syslib.o (assembler files
     had no dependency on asm_dec.s), the block was too small and copying
     the ticker code below overran it.  */
  if (__pthread_callevery_block_size
      != sizeof (struct __pthread_callevery_block))
    __unixlib_fatal ("UnixLib was built from mismatched objects "
		     "(pthread RMA block size); rebuild it from clean");

  strcpy (gbl->pthread_callevery_rma->filter_name, filter_name);

  /* The ticker routines run from SharedUnixLibrary or the RMA block
     (pthread/ticker.c).  */
  __pthread_ticker_init ();

  __pthread_thread_list = __pthread_running_thread;
  gbl->pthread_num_running_threads = 1;
}

/* Called once, at program finalisation.  */
void attribute_hidden
__pthread_prog_fini (void)
{
  struct ul_global *gbl = &__ul_global;
#ifdef PTHREAD_DEBUG
  debug_printf ("-- __pthread_prog_fini: Program finalisation\n");
#endif

  /* 2026: in a fork()/vfork() child (its _exit, e.g. after a failed exec),
     leave the parent's things alone.  The child used to free the parent's
     RMA block and detach it from PThreadTicker; the parent then restarted
     its ticker on the freed block (__fork_post), which now holds running
     code and counters.  The parent stopped its ticker before the child
     ran and restarts it afterwards; if the child started it, stop it.  */
  if (!__pthread_ticker_owner ())
    {
      if (gbl->pthread_system_running)
	__pthread_stop_ticker ();
      gbl->pthread_system_running = 0;
      return;
    }

  /* 2026: before the ticker is stopped (UnixLib$TickerStats).  */
  __pthread_ticker_write_stats ();

  /* pthread timers must be stopped */
  if (gbl->pthread_system_running)
    {
      __pthread_stop_ticker ();
      gbl->pthread_system_running = 0;
    }
  __pthread_ticker_fini ();

  /* Free the RMA block that was allocated in __pthread_prog_init */
  int regs[10];
  
  regs[0] = 7;
  regs[2] = (int) gbl->pthread_callevery_rma;
  __os_swi (OS_Module, regs);
}
