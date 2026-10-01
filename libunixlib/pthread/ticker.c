/* Thread ticker: starting and stopping it, the Wimp filters, statistics.
   Copyright (c) 2026 UnixLib Developers.

   2026: this was assembler in _context.s.  Background, the reasons for the
   design and how to read the statistics: docs/THREAD-TICKER.md.

   With more than one thread, an OS_CallEvery ticker sets a callback every
   2 cs to switch threads.  In a Wimp task, Wimp filters stop the ticker
   when the program calls Wimp_Poll and start it when Wimp_Poll returns.
   The ticker routines themselves (internal/ticker.s) run from the
   PThreadTicker module when it is loaded (module/pthticker.s) or from a
   copy in the RMA block: never from the program, which isn't paged in
   when the ticker fires in another task.

   If the system variable UnixLib$TickerStats names a file, a line of
   counters is appended to it when the program exits.  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <swis.h>

#include <pthread.h>
#include <internal/os.h>
#include <internal/unix.h>

/* The PThreadTicker module's interface table, after its header.  */
#define TICKER_MODULE "PThreadTicker"
#define TICKER_MAGIC 0x6B545450		/* "PTTk" */
struct ticker_interface
{
  unsigned magic, version, entries;
  unsigned handler, start, stop, prefilter, postfilter, attach, detach;
};

/* _context.s: the routines to copy, where they are in the copy (start,
   stop, pre-filter, post-filter), and a way to call one.  */
extern const unsigned __pthread_ticker_offsets[4];
extern void __pthread_ticker_call (void *__block, const void *__routine);

/* The routines in use: the module's or the RMA copy's.  */
static const void *start_routine, *stop_routine, *pre_filter, *post_filter;
static void *module_ws;		/* The module's workspace, if used.  */
static const void *detach_routine;
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
  if (_swix (Wimp_ReadSysInfo, _IN(0) | _OUTR(0,1), 5, handle, version)
      || *handle == 0)
    *handle = *version = 0;	/* R1 is undefined when not a task.  */
}

static inline struct __pthread_callevery_block *
block (void)
{
  return __ul_global.pthread_callevery_rma;
}

/* The PThreadTicker module's interface, or NULL if it isn't loaded or
   is not one we know.  */
static const struct ticker_interface *
find_module (const char **base, void **ws)
{
  const struct ticker_interface *t;

  if (_swix (OS_Module, _INR(0,1) | _OUTR(3,4), 18, TICKER_MODULE, base, ws)
      || *base == NULL || *ws == NULL)
    return NULL;
  t = (const struct ticker_interface *) (*base + 0x34);
  if (t->magic != TICKER_MAGIC || t->version != 1 || t->entries < 7)
    return NULL;
  return t;
}

/* 2026: the process that claimed the RMA block and attached to the
   module (SUL's pid, unique while the process exists).  A fork()/vfork()
   child has a copy of, or shares, these variables, but the block and the
   attachment belong to the parent.  */
static pid_t owner_pid;

/* Non-zero in the process that set the ticker up, zero in a fork()/vfork()
   child of it.  Also non-zero if __pthread_ticker_init never ran (a fatal
   error early in __pthread_prog_init): then the block is this process's
   and should still be freed, as before.  */
int
__pthread_ticker_owner (void)
{
  return owner_pid == 0 || getpid () == owner_pid;
}

/* Called once by __pthread_prog_init: use the PThreadTicker module's
   routines if it is loaded, else copy them into the RMA block.  */
void
__pthread_ticker_init (void)
{
  struct __pthread_callevery_block *b = block ();
  const struct ticker_interface *t;
  const char *base;
  void *ws;

  owner_pid = getpid ();

  __pthread_ticker_read_task (&startup_handle, &startup_version);

  if ((t = find_module (&base, &ws)) != NULL)
    {
      b->flags |= 1;
      start_routine = base + t->start;
      stop_routine = base + t->stop;
      pre_filter = base + t->prefilter;
      post_filter = base + t->postfilter;
      detach_routine = base + t->detach;
      module_ws = ws;
      /* The module won't be killed while we're attached.  */
      __pthread_ticker_call (ws, base + t->attach);
    }
  else
    {
      char *code = (char *) b->ticker_code;
      size_t len = __pthread_call_every_code_end - __pthread_call_every_code;

      memcpy (code, __pthread_call_every_code, len);
      _swix (OS_SynchroniseCodeAreas, _INR(0,2), 1, code, code + len - 1);
      start_routine = code + __pthread_ticker_offsets[0];
      stop_routine = code + __pthread_ticker_offsets[1];
      pre_filter = code + __pthread_ticker_offsets[2];
      post_filter = code + __pthread_ticker_offsets[3];
    }
}

static void
ticker_on (struct __pthread_callevery_block *b)
{
  __pthread_ticker_call (b, start_routine);
}

static void
ticker_off (struct __pthread_callevery_block *b)
{
  __pthread_ticker_call (b, stop_routine);
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

/* Called by __pthread_prog_fini once the ticker is stopped and the filters
   removed, before the RMA block is freed: let the module go.  */
void
__pthread_ticker_fini (void)
{
  /* Not in a fork()/vfork() child: the attachment is the parent's.  */
  if (module_ws != NULL && __pthread_ticker_owner ())
    {
      __pthread_ticker_call (module_ws, detach_routine);
      module_ws = NULL;
    }
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

  if (b == NULL || file == NULL || file[0] == '\0'
      || !__pthread_ticker_owner ())
    return;

  /* The program name: the leaf of the first word of the command line
     (the whole path was cut short: "...!Warzone2100.warzone210").  */
  prog[0] = '\0';
  if (!_swix (OS_GetEnv, _OUT(0), &cmd) && cmd != NULL)
    {
      const char *leaf = cmd;

      for (i = 0; cmd[i] > ' '; i++)
	if (cmd[i] == '.' || cmd[i] == ':')
	  leaf = cmd + i + 1;
      for (i = 0; i < (int) sizeof (prog) - 1 && leaf[i] > ' '; i++)
	prog[i] = leaf[i];
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
		(b->flags & 1) ? "module" : "RMA");
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
