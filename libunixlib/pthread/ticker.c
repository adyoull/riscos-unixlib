/* Thread ticker: starting and stopping it, the Wimp filters, statistics.
   Copyright (c) 2026 UnixLib Developers.

   2026: this was assembler in _context.s.  Background, the reasons for the
   design and how to read the statistics: docs/THREAD-TICKER.md.

   With more than one thread, an OS_CallEvery ticker sets a callback every
   2 cs to switch threads.  In a Wimp task, Wimp filters stop the ticker
   when the program calls Wimp_Poll and start it when Wimp_Poll returns.
   The ticker routines themselves (internal/ticker.s) run from
   SharedUnixLibrary 1.17+ (SharedUnixLibrary_Ticker) or, with an older
   SUL, from a copy in the RMA block: never from the program, which isn't
   paged in when the ticker fires in another task.

   If the system variable UnixLib$TickerStats names a file, a line of
   counters is appended to it when the program exits.  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <swis.h>

#include <pthread.h>
#include <internal/os.h>
#include <internal/unix.h>

#ifndef SharedUnixLibrary_Ticker
#define SharedUnixLibrary_Ticker 0x55c85
#endif
enum
{
  TICKER_START, TICKER_STOP, TICKER_RELEASE, TICKER_ROUTINES
};

/* _context.s: the routines to copy, where they are in the copy (start,
   stop, pre-filter, post-filter), and a way to call one.  */
extern const unsigned __pthread_ticker_offsets[4];
extern void __pthread_ticker_call (void *__block, const void *__routine);

static void *sul_key;		/* Our SharedUnixLibrary key.  */
static const void *pre_filter, *post_filter;
static int filters_for;		/* Task the filters are registered for.  */
static volatile int busy;	/* In __pthread_start/stop_ticker.  */

/* For the statistics.  */
static int startup_handle, startup_version;
static int first_start_handle = -1;
static unsigned starts, filter_moves, filter_errors;

/* Task handle and Wimp version (Wimp_ReadSysInfo 5), 0 when not in the
   desktop or not a Wimp task.  */
void
__pthread_ticker_read_task (int *handle, int *version)
{
  int desktop = 0;

  *handle = *version = 0;
  if (_swix (Wimp_ReadSysInfo, _IN(0) | _OUT(0), 3, &desktop) || !desktop)
    return;
  if (_swix (Wimp_ReadSysInfo, _IN(0) | _OUTR(0,1), 5, handle, version))
    *handle = *version = 0;
}

static inline struct __pthread_callevery_block *
block (void)
{
  return __ul_global.pthread_callevery_rma;
}

/* Called once by __pthread_prog_init: use SharedUnixLibrary's ticker if
   it has one, else copy the routines into the RMA block.  */
void
__pthread_ticker_init (void)
{
  struct __pthread_callevery_block *b = block ();
  const void *pre, *post;

  __pthread_ticker_read_task (&startup_handle, &startup_version);
  sul_key = b->sul_upcall_r12;

  if (!_swix (SharedUnixLibrary_Ticker, _INR(0,1) | _OUTR(1,2),
	      TICKER_ROUTINES, sul_key, &pre, &post))
    {
      b->flags |= 1;
      pre_filter = pre;
      post_filter = post;
    }
  else
    {
      char *code = (char *) b->ticker_code;
      size_t len = __pthread_call_every_code_end - __pthread_call_every_code;

      memcpy (code, __pthread_call_every_code, len);
      _swix (OS_SynchroniseCodeAreas, _INR(0,2), 1, code, code + len - 1);
      pre_filter = code + __pthread_ticker_offsets[2];
      post_filter = code + __pthread_ticker_offsets[3];
    }
}

static void
ticker_on (struct __pthread_callevery_block *b)
{
  if (b->flags & 1)
    _swix (SharedUnixLibrary_Ticker, _INR(0,2), TICKER_START, sul_key, b);
  else
    __pthread_ticker_call (b, (char *) b->ticker_code
			      + __pthread_ticker_offsets[0]);
}

static void
ticker_off (struct __pthread_callevery_block *b)
{
  if (b->flags & 1)
    _swix (SharedUnixLibrary_Ticker, _INR(0,1), TICKER_STOP, sul_key);
  else
    __pthread_ticker_call (b, (char *) b->ticker_code
			      + __pthread_ticker_offsets[1]);
}

/* Register the filters for task HANDLE (0: none), removing them from the
   task they were registered for.  Before 2026 the handle was read once at
   start-up (before Wimp_Initialise) and only re-read while it was 0, and
   the filters were registered only when a thread was created.  */
static void
set_filters (struct __pthread_callevery_block *b, int handle)
{
  if (handle == filters_for)
    return;

  if (filters_for != 0)
    {
      _swix (Filter_DeRegisterPreFilter, _INR(0,3), b->filter_name,
	     pre_filter, b, filters_for);
      _swix (Filter_DeRegisterPostFilter, _INR(0,4), b->filter_name,
	     post_filter, b, filters_for, 0);
      filters_for = 0;
    }

  if (handle != 0)
    {
      if (_swix (Filter_RegisterPreFilter, _INR(0,3), b->filter_name,
		 pre_filter, b, handle))
	filter_errors++;
      else if (_swix (Filter_RegisterPostFilter, _INR(0,4), b->filter_name,
		      post_filter, b, handle, 0))
	{
	  filter_errors++;
	  _swix (Filter_DeRegisterPreFilter, _INR(0,3), b->filter_name,
		 pre_filter, b, handle);
	}
      else
	{
	  filters_for = handle;
	  b->filter_handle = handle;
	  filter_moves++;
	}
    }
}

/* The task handle now; also updates the cached one.  */
static int
current_task (void)
{
  int handle, version;

  __pthread_ticker_read_task (&handle, &version);
  if (handle != 0)
    __ul_global.taskhandle = handle;
  return handle;
}

/* Start the ticker if there's more than one thread.  */
void
__pthread_start_ticker (void)
{
  struct ul_global *gbl = &__ul_global;
  struct __pthread_callevery_block *b = block ();

  if (!gbl->pthread_system_running || gbl->pthread_num_running_threads <= 1)
    return;

  busy = 1;
  set_filters (b, current_task ());
  ticker_on (b);
  if (first_start_handle == -1)
    first_start_handle = filters_for;
  starts++;
  busy = 0;
}

/* Stop the ticker and remove the filters.  */
void
__pthread_stop_ticker (void)
{
  struct __pthread_callevery_block *b = block ();

  if (!__ul_global.pthread_system_running)
    return;

  busy = 1;
  set_filters (b, 0);
  ticker_off (b);
  busy = 0;
}

/* Called now and then by the context switcher (USR mode, in the
   callback): if the program has become a Wimp task, or its task handle
   has changed, since the filters were registered, move them.  Threads
   started before Wimp_Initialise (an SDL timer or audio thread, say) used
   to leave the ticker running with no filters at all.  */
void
__pthread_ticker_recheck (void)
{
  struct ul_global *gbl = &__ul_global;

  if (busy || !gbl->pthread_system_running
      || gbl->pthread_num_running_threads <= 1)
    return;
  set_filters (block (), current_task ());
}

/* Called by __pthread_prog_fini once the ticker is stopped, before the RMA
   block is freed.  */
void
__pthread_ticker_fini (void)
{
  if (block ()->flags & 1)
    _swix (SharedUnixLibrary_Ticker, _INR(0,1), TICKER_RELEASE, sul_key);
}

/* Append one line to the file named by UnixLib$TickerStats.  Called by
   __pthread_prog_fini, before the RMA block is freed.  Uses the file
   SWIs, not stdio: this runs very late in exit.  */
void
__pthread_ticker_write_stats (void)
{
  const struct __pthread_callevery_block *b
    = __ul_global.pthread_callevery_rma;
  const char *file = getenv ("UnixLib$TickerStats");
  static char line[400];
  char prog[48];
  const char *cmd = NULL;
  int handle = 0, version = 0, fh = 0, ext = 0, created = 0, n, i;

  if (b == NULL || file == NULL || file[0] == '\0')
    return;

  /* The program name: the first word of the command line.  */
  prog[0] = '\0';
  if (!_swix (OS_GetEnv, _OUT(0), &cmd) && cmd != NULL)
    {
      for (i = 0; i < (int) sizeof (prog) - 1 && cmd[i] > ' '; i++)
	prog[i] = cmd[i];
      prog[i] = '\0';
    }

  __pthread_ticker_read_task (&handle, &version);
  n = snprintf (line, sizeof (line),
		"%s ticks=%u foreign=%u last_foreign=%p/%p pre=%u post=%u"
		" filters_for=%#x startup=%#x/%d cached=%#x now=%#x/%d"
		" first_start=%#x starts=%u filter_moves=%u filter_errors=%u"
		" via=%s\n",
		prog, b->ticks, b->foreign_ticks, b->foreign_handler,
		b->foreign_r12, b->pre_calls, b->post_calls,
		b->filter_handle, startup_handle, startup_version,
		__ul_global.taskhandle, handle, version,
		first_start_handle == -1 ? 0 : first_start_handle, starts,
		filter_moves, filter_errors,
		(b->flags & 1) ? "SUL" : "RMA");
  if (n <= 0)
    return;
  if (n >= (int) sizeof (line))
    n = sizeof (line) - 1;

  /* Open for update (no path, error if absent), else create.  */
  if (_swix (OS_Find, _INR(0,1) | _OUT(0), 0xC7, file, &fh) || fh == 0)
    {
      if (_swix (OS_Find, _INR(0,1) | _OUT(0), 0x83, file, &fh) || fh == 0)
	return;
      created = 1;
    }
  if (!_swix (OS_Args, _INR(0,1) | _OUT(2), 2, fh, &ext))
    _swix (OS_Args, _INR(0,2), 1, fh, ext);
  _swix (OS_GBPB, _INR(0,3), 2, fh, line, n);
  _swix (OS_Find, _INR(0,1), 0, fh);
  if (created)
    _swix (OS_File, _INR(0,2), 18, file, 0xFFF);	/* Text */
}
