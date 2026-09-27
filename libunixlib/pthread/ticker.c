/* Thread ticker: statistics.
   Copyright (c) 2026 UnixLib Developers.

   2026: the ticker was found running while another Wimp task was paged in
   (Warzone 2100 on the Pi; docs/THREAD-TICKER.md).  To find out why, the
   ticker routines count what they do in the RMA block, and if the system
   variable UnixLib$TickerStats names a file, a line is appended to it when
   the program exits.  Nothing is written otherwise.  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <swis.h>

#include <pthread.h>
#include <internal/os.h>
#include <internal/unix.h>

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

/* What Wimp_ReadSysInfo 5 said at start-up (UnixLib caches that handle
   in __ul_global.taskhandle).  */
static int startup_handle, startup_version;

void
__pthread_ticker_note_startup (void)
{
  __pthread_ticker_read_task (&startup_handle, &startup_version);
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
		" via=%s\n",
		prog, b->ticks, b->foreign_ticks, b->foreign_handler,
		b->foreign_r12, b->pre_calls, b->post_calls,
		b->filter_handle, startup_handle, startup_version,
		__ul_global.taskhandle, handle, version,
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
