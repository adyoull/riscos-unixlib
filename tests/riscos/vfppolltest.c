/* VFPPoll: which VFP context is active when Wimp_Poll returns, after
   other UnixLib programs have started and exited (2026-11-11, from the
   Web Browser project's report: Strada's browser task stopped with
   "Illegal Instruction" on the first VFP instruction after a Wimp poll on
   native RISC OS 5.31).

   A Wimp task.  50 times: start a copy of itself as another task
   (Wimp_StartTask "... child"; the child does some floating point and
   exits without polling, so SharedUnixLibrary's exit turns VFP off), then
   poll for 5 null events.  After every Wimp_Poll and Wimp_StartTask
   return, before any floating point, it reads VFPSupport_ActiveContext
   and counts whether that is this program's context, another one, or
   none, and then does a floating-point calculation.  An undefined
   instruction there ends the run (SIGILL is caught and logged).

   "reclaim" calls __pthread_vfp_reclaim () first, as a Thumb program
   should (5.0.3.8): then the counts must all be "own".
   "threads" as an argument adds a second thread (so UnixLib's ticker
   and its Wimp filters run).  Built as ARM (vfppolltest) and as Thumb
   (vfppolltestt): VFPSupport's lazy activation behaves differently for
   Thumb code.

   The log goes to <Wimp$ScrapDir>.VFPPoll (appended), and a summary is
   shown in an error box.  */
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <kernel.h>
#include <swis.h>
#include <unixlib/local.h>

#ifndef VFPSupport_ActiveContext
#define VFPSupport_ActiveContext 0x58EC6
#endif

static FILE *logf;
static const char *where = "start";
static int own, same, other, none, polls, starts;
static volatile double sink;
static int reclaim;

static void
logline (const char *s)
{
  if (logf)
    {
      fputs (s, logf);
      fputc ('\n', logf);
      fflush (logf);
    }
}

static void
report (const char *verdict)
{
  char line[256];
  snprintf (line, sizeof line,
	    "%s%s %s: %d polls, %d task starts; active context afterwards: "
	    "own %d, another %d, none %d",
#ifdef __thumb__
	    "Thumb",
#else
	    "ARM",
#endif
	    reclaim ? " reclaim" : "", verdict, polls, starts, same, other, none);
  logline (line);
  _kernel_oserror e;
  e.errnum = 0;
  snprintf (e.errmess, sizeof e.errmess, "VFPPoll %s", line);
  _swix (Wimp_ReportError, _INR(0,2), &e, 1, "VFPPoll");
}

static void
on_sigill (int sig)
{
  (void) sig;
  char line[128];
  snprintf (line, sizeof line, "FAIL: Illegal instruction on the first "
	    "floating point after %s", where);
  logline (line);
  report ("FAIL");
  _swix (Wimp_CloseDown, 0);
  _exit (1);
}

/* Straight after Wimp_Poll or Wimp_StartTask returns: no floating point
   before the context is read.  */
static void __attribute__ ((noinline))
check (const char *what)
{
  int cur = 0;
  _swix (VFPSupport_ActiveContext, _OUT(0), &cur);
  where = what;
  if (cur == own)
    same++;
  else if (cur == 0)
    none++;
  else
    other++;
  sink = sink * 1.0000001 + 0.5;	/* VFP */
}

static void *
spinner (void *arg)
{
  (void) arg;
  double d = 1.0;
  for (;;)
    {
      d = d * 1.0000001 + 0.25;
      sink = d;
      usleep (10000);
    }
  return NULL;
}

int
main (int argc, char **argv)
{
  if (argc > 1 && strcmp (argv[1], "child") == 0)
    {
      volatile double d = 2.5;
      for (int i = 0; i < 1000; i++)
	d = d * 1.001 + 0.5;
      return d > 0 ? 0 : 1;
    }

  int threads = 0;
  for (int a = 1; a < argc; a++)
    {
      if (strcmp (argv[a], "threads") == 0)
	threads = 1;
      else if (strcmp (argv[a], "reclaim") == 0)
	reclaim = 1;
    }
  logf = fopen ("<Wimp$ScrapDir>.VFPPoll", "a");
  _swix (VFPSupport_ActiveContext, _OUT(0), &own);
  {
    char line[160];
    snprintf (line, sizeof line, "== VFPPoll (%s%s), own context %08x",
#ifdef __thumb__
	      "Thumb",
#else
	      "ARM",
#endif
	      threads ? (reclaim ? ", 2 threads, reclaim" : ", 2 threads")
		      : (reclaim ? ", reclaim" : ""), own);
    logline (line);
  }
  signal (SIGILL, on_sigill);

  int task;
  if (_swix (Wimp_Initialise, _INR(0,3) | _OUT(1), 310, 0x4B534154,
	     "VFPPoll", 0, &task) != NULL)
    {
      logline ("FAIL: Wimp_Initialise");
      return 1;
    }

  pthread_t t;
  if (threads)
    pthread_create (&t, NULL, spinner, NULL);

  char cmd[300];
  /* The child is started through the Obey file VFPChild (its own
     wimpslot), which the Obey files name in VFPPoll$Child.  */
  const char *child = getenv ("VFPPoll$Child");
  if (child == NULL)
    {
      logline ("FAIL: VFPPoll$Child isn't set (run it from its Obey file)");
      return 1;
    }
  snprintf (cmd, sizeof cmd, "%s", child);
  static int block[64];
  for (int round = 0; round < 50; round++)
    {
      _swix (Wimp_StartTask, _IN(0), cmd);
      if (reclaim)
	__pthread_vfp_reclaim ();
      check ("Wimp_StartTask");
      starts++;
      for (int i = 0; i < 5; i++)
	{
	  int reason;
	  _swix (Wimp_Poll, _INR(0,1) | _OUT(0), 0, block, &reason);
	  if (reclaim)
	    __pthread_vfp_reclaim ();
	  check ("Wimp_Poll");
	  polls++;
	  if (reason == 17 || reason == 18)	/* Message_Quit */
	    if (block[4] == 0)
	      goto done;
	}
    }
done:
  report (none || other ? "DIFFERENT" : "PASS");
  _swix (Wimp_CloseDown, _INR(0,1), task, 0x4B534154);
  if (logf)
    fclose (logf);
  return 0;
}
