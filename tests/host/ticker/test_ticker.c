/* Host tests for pthread/ticker.c with fake SWIs: the PThreadTicker
   module's routines or the RMA copy, the Wimp filters following the task
   handle, and the UnixLib$TickerStats line.  */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swis.h"
#include "pthread.h"
#include "internal/unix.h"

/* A fake PThreadTicker module: header, then the interface table at &34;
   the routines "are" at &80.. (never run here, only called by address).  */
static unsigned modbuf[64] = {
  [0x34 / 4] = 0x6B545450, 1, 7,
  0x80, 0x84, 0x88, 0x8c, 0x90, 0x94, 0x98
};
static int modws;			/* Its workspace.  */
#define MOD ((int) (long) modbuf)
#define WS ((int) (long) &modws)
#define PRE_MOD (MOD + 0x8c)
#define POST_MOD (MOD + 0x90)
#define CALL 0x999			/* Logged __pthread_ticker_call.  */

struct ul_global __ul_global;
static struct __pthread_callevery_block blk;

/* The routines _context.s would provide: 256 bytes of "code" and the
   offsets of start, stop, pre-filter, post-filter in it.  */
__asm__ (".section .rodata\n"
	 ".globl __pthread_call_every_code\n"
	 ".globl __pthread_call_every_code_end\n"
	 "__pthread_call_every_code: .fill 256,1,0x5a\n"
	 "__pthread_call_every_code_end: .byte 0\n"
	 ".text\n");
const unsigned __pthread_ticker_offsets[4] = { 0x6c, 0xa0, 0xd0, 0xe8 };
static void log_call (int swi, const int *in);
void
__pthread_ticker_call (void *block, const void *routine)
{
  int in[6] = { (int) (long) block, (int) (long) routine };
  log_call (CALL, in);
}

/* Fake RISC OS state.  */
static int have_mod, desktop, handle, version, fail_pre, fail_post;
static char *env_stats;
static char file[2048];
static int file_exists, file_type;
static _kernel_oserror err = { 1, "x" };

char *
fake_getenv (const char *n)
{
  return strcmp (n, "UnixLib$TickerStats") == 0 ? env_stats : NULL;
}

/* SWI log.  */
static struct { int swi, in[6]; } log_[64];
static int nlog;

static void
log_call (int swi, const int *in)
{
  if (nlog < 64)
    {
      log_[nlog].swi = swi;
      memcpy (log_[nlog].in, in, sizeof (log_[nlog].in));
      nlog++;
    }
}

const _kernel_oserror *
_swix (int swi, unsigned mask, ...)
{
  int in[10] = { 0 }, *out[10] = { 0 }, i;
  va_list ap;

  va_start (ap, mask);
  for (i = 0; i < 10; i++)
    if (mask & (1U << i))
      in[i] = va_arg (ap, int);
  for (i = 0; i < 10; i++)
    if (mask & (1U << (31 - i)))
      out[i] = va_arg (ap, int *);
  va_end (ap);
  swi &= ~0x20000;
  if (swi != Wimp_ReadSysInfo)
    log_call (swi, in);

  switch (swi)
    {
    case Wimp_ReadSysInfo:
      if (in[0] == 3)
	*out[0] = desktop;
      else if (in[0] == 5)
	{
	  *out[0] = handle;
	  *out[1] = version;
	}
      return NULL;
    case OS_Module:
      if (in[0] != 18 || !have_mod
	  || strcmp ((const char *) (long) in[1], "PThreadTicker") != 0)
	return &err;
      *out[3] = MOD;
      *out[4] = WS;
      return NULL;
    case Filter_RegisterPreFilter:
      return fail_pre ? &err : NULL;
    case Filter_RegisterPostFilter:
      return fail_post ? &err : NULL;
    case Filter_DeRegisterPreFilter:
    case Filter_DeRegisterPostFilter:
    case OS_SynchroniseCodeAreas:
      return NULL;
    case OS_GetEnv:
      *out[0] = (int) (long) "SDFS::ePic.$.Apps.Games.!Warzone2100.tickprog -early";
      return NULL;
    case OS_Find:
      if (in[0] == 0)
	return NULL;
      if (in[0] == 0xC7 && !file_exists)
	return &err;
      if (in[0] == 0x83)
	file[0] = '\0', file_exists = 1;
      *out[0] = 5;
      return NULL;
    case OS_Args:
      if (in[0] == 2)
	*out[2] = strlen (file);
      return NULL;
    case OS_GBPB:
      strncat (file, (const char *) (long) in[2], in[3]);
      return NULL;
    case OS_File:
      file_type = in[2];
      return NULL;
    }
  fprintf (stderr, "bad swi %x\n", swi);
  abort ();
}

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; \
  printf ("FAIL %d: ", __LINE__); printf (__VA_ARGS__); printf ("\n"); } } while (0)

/* How many logged calls of SWI with in[0..n) as given.  */
static int
calls (int swi, int n, ...)
{
  int want[6], i, j, count = 0;
  va_list ap;

  va_start (ap, n);
  for (i = 0; i < n; i++)
    want[i] = va_arg (ap, int);
  va_end (ap);
  for (i = 0; i < nlog; i++)
    if (log_[i].swi == swi)
      {
	for (j = 0; j < n && log_[i].in[j] == want[j]; j++)
	  ;
	count += j == n;
      }
  return count;
}
#define B ((int) (long) &blk)
#define NAME ((int) (long) blk.filter_name)

static void
setup (int mod)
{
  memset (&blk, 0, sizeof (blk));
  strcpy (blk.filter_name, "UnixLib pthread");
  __ul_global.pthread_callevery_rma = &blk;
  __ul_global.pthread_system_running = 1;
  __ul_global.pthread_num_running_threads = 1;
  __ul_global.taskhandle = 0;
  have_mod = mod;
  fail_pre = fail_post = 0;
  nlog = 0;
  __pthread_ticker_init ();
}

int
main (void)
{
  const char *code = (const char *) blk.ticker_code;

  /* The module is loaded: its routines, no copy, attached.  */
  desktop = 1, handle = 0x1111, version = 310;
  setup (1);
  CHECK (blk.flags == 1, "module found");
  CHECK (calls (CALL, 2, WS, MOD + 0x94) == 1, "attached to the module");
  CHECK (calls (OS_SynchroniseCodeAreas, 0) == 0 && blk.ticker_code[0] == 0,
	 "nothing copied with the module");

  nlog = 0;
  __pthread_start_ticker ();
  CHECK (nlog == 0, "one thread: no ticker");
  __ul_global.pthread_num_running_threads = 2;
  __pthread_start_ticker ();
  CHECK (calls (Filter_RegisterPreFilter, 4, NAME, PRE_MOD, B, 0x1111) == 1
	 && calls (Filter_RegisterPostFilter, 5, NAME, POST_MOD, B, 0x1111, 0)
	 == 1, "filters for task 0x1111 with the module's routines");
  CHECK (calls (CALL, 2, B, MOD + 0x84) == 1, "module's start routine");
  CHECK (__ul_global.taskhandle == 0x1111, "cached task handle updated");

  nlog = 0;
  __pthread_start_ticker ();
  CHECK (calls (Filter_RegisterPreFilter, 0) == 0, "no second registration");

  /* The task handle changes: the filters move.  */
  nlog = 0;
  __pthread_ticker_recheck ();
  CHECK (nlog == 0, "recheck, same task: nothing");
  handle = 0x2222;
  __pthread_ticker_recheck ();
  CHECK (calls (Filter_DeRegisterPreFilter, 4, NAME, PRE_MOD, B, 0x1111) == 1
	 && calls (Filter_DeRegisterPostFilter, 5, NAME, POST_MOD, B, 0x1111, 0)
	 == 1, "old filters removed with the old handle");
  CHECK (calls (Filter_RegisterPreFilter, 4, NAME, PRE_MOD, B, 0x2222) == 1
	 && calls (Filter_RegisterPostFilter, 4, NAME, POST_MOD, B, 0x2222) == 1,
	 "new filters for 0x2222");
  CHECK (blk.filter_handle == 0x2222 && __ul_global.taskhandle == 0x2222,
	 "handles noted");

  nlog = 0;
  __pthread_stop_ticker ();
  CHECK (calls (Filter_DeRegisterPreFilter, 4, NAME, PRE_MOD, B, 0x2222) == 1,
	 "stop removes the filters");
  CHECK (calls (CALL, 2, B, MOD + 0x88) == 1, "module's stop routine");
  CHECK (blk.filter_handle == 0x2222, "filter_handle keeps the last one");

  /* Threads before Wimp_Initialise: no filters, until the recheck.  */
  handle = 0;
  nlog = 0;
  __pthread_start_ticker ();
  CHECK (calls (Filter_RegisterPreFilter, 0) == 0
	 && calls (CALL, 2, B, MOD + 0x84) == 1,
	 "not a task yet: ticker, no filters");
  handle = 0x3333;
  __ul_global.pthread_num_running_threads = 1;
  __pthread_ticker_recheck ();
  CHECK (calls (Filter_RegisterPreFilter, 0) == 0, "recheck ignored with one thread");
  __ul_global.pthread_num_running_threads = 3;
  __pthread_ticker_recheck ();
  CHECK (calls (Filter_RegisterPreFilter, 4, NAME, PRE_MOD, B, 0x3333) == 1,
	 "recheck registers them once we're a task");

  /* Handle goes back to 0 (Wimp_CloseDown): the filters go, the cached
     handle stays.  */
  handle = 0;
  nlog = 0;
  __pthread_ticker_recheck ();
  CHECK (calls (Filter_DeRegisterPreFilter, 4, NAME, PRE_MOD, B, 0x3333) == 1
	 && calls (Filter_RegisterPreFilter, 0) == 0, "no task: filters removed");
  CHECK (__ul_global.taskhandle == 0x3333, "cached handle kept");

  /* Registration failures aren't fatal.  */
  handle = 0x4444;
  fail_post = 1;
  nlog = 0;
  __pthread_ticker_recheck ();
  CHECK (calls (Filter_DeRegisterPreFilter, 4, NAME, PRE_MOD, B, 0x4444) == 1,
	 "post-filter failed: pre-filter removed again");
  fail_post = 0;
  nlog = 0;
  __pthread_ticker_recheck ();
  CHECK (calls (Filter_RegisterPostFilter, 4, NAME, POST_MOD, B, 0x4444) == 1,
	 "and tried again at the next recheck");

  /* Statistics.  */
  blk.ticks = 500, blk.foreign_ticks = 7, blk.pre_calls = 40, blk.post_calls = 39;
  env_stats = "RAM::RamDisc0.$.Stats";
  file_exists = 0;
  __pthread_ticker_write_stats ();
  CHECK (strncmp (file, "tickprog ticks=500 foreign=7 ", 29) == 0
	 && strstr (file, " pre=40 post=39 ") && strstr (file, "filters_for=0x4444")
	 && strstr (file, "startup=0x1111/310") && strstr (file, "first_start=0x1111")
	 && strstr (file, "filter_moves=4") && strstr (file, "filter_errors=1")
	 && strstr (file, "via=module\n"), "stats line: %s", file);
  CHECK (file_type == 0xFFF, "new stats file typed Text");
  file_type = 0;
  __pthread_ticker_write_stats ();
  CHECK (strstr (strchr (file, '\n') + 1, "tickprog ticks=500") && file_type == 0,
	 "second line appended");
  /* Not a task any more: R1 is garbage then, and must not be printed.  */
  handle = 0, version = -1073741823;
  file[0] = '\0', file_exists = 0;
  __pthread_ticker_write_stats ();
  CHECK (strncmp (file, "tickprog ", 9) == 0 && strstr (file, " now=0/0 "),
	 "leaf name, no version without a task: %s", file);
  version = 310;
  env_stats = NULL;
  nlog = 0;
  __pthread_ticker_write_stats ();
  CHECK (nlog == 0, "no variable: nothing written");

  nlog = 0;
  __pthread_stop_ticker ();
  __ul_global.pthread_system_running = 0;
  __pthread_ticker_fini ();
  CHECK (calls (CALL, 2, WS, MOD + 0x98) == 1, "fini: detached");

  /* A module with the wrong magic is ignored.  */
  modbuf[0x34 / 4] = 0x12345678;
  setup (1);
  CHECK (blk.flags == 0, "wrong magic: module not used");
  modbuf[0x34 / 4] = 0x6B545450;

  /* No module: copy and run the copy.  */
  handle = 0x5555;
  setup (0);
  CHECK (blk.flags == 0 && memcmp (code, __pthread_call_every_code, 256) == 0
	 && calls (OS_SynchroniseCodeAreas, 3, 1, (int) (long) code,
		   (int) (long) code + 255) == 1, "no module: routines copied");
  __ul_global.pthread_num_running_threads = 2;
  __pthread_start_ticker ();
  CHECK (calls (Filter_RegisterPreFilter, 4, NAME, (int) (long) code + 0xd0,
		B, 0x5555) == 1
	 && calls (Filter_RegisterPostFilter, 4, NAME, (int) (long) code + 0xe8,
		   B, 0x5555) == 1, "filters point into the copy");
  CHECK (calls (CALL, 2, B, (int) (long) code + 0x6c) == 1, "start runs the copy");
  __pthread_stop_ticker ();
  CHECK (calls (CALL, 2, B, (int) (long) code + 0xa0) == 1, "stop runs the copy");
  nlog = 0;
  __ul_global.pthread_system_running = 0;
  __pthread_ticker_fini ();
  CHECK (nlog == 0, "fini: nothing to release");

  printf ("ticker: %d checks, %d failed\n", checks, fails);
  return fails != 0;
}
