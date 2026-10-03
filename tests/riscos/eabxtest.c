/* eabxtest: does ARMEABISupport still know this program when it exits?
   (riscos-unixlib, 2026-10-03)

   ARMEABISupport identifies a program ("app") by the physical address of
   its page at &8000.  At exit, SharedUnixLibrary frees the main stack and
   calls ARMEABISupport_Cleanup, which find the app the same way.  If that
   page has changed, both fail silently and the app's record (and its
   stacks) stay behind until a reboot; a later program that gets the same
   page then starts without abort handlers, and its first stack-growth
   fault is an EMT trap ("code 6").  Reel (a Wimp task with threads that
   calls Wimp_CloseDown, then exit) left such a record on a Pi; NoHW (a
   plain command-line program) didn't.

   This program logs, at each stage, the physical page at &8000 and whether
   ARMEABISupport still finds this program's stack (StackOp 2, which uses
   the same lookup):
     start, after Wimp_Initialise, after polling, after Wimp_CloseDown,
     and in an atexit handler (the last thing before SharedUnixLibrary's
     exit code).

     eabxtest [-nowimp] [-nothreads] [-noclose] [-nojoin] [-s secs]
   -nojoin    exit with the two threads still running (as a program
              whose audio thread is still busy when it quits)

   Results are appended to <Wimp$ScrapDir>.EABIExit.  Afterwards,
   *ARMEABISupport_Info should list no App left by it.  */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <kernel.h>
#include <swis.h>

static FILE *logf;
static volatile int stop;
static unsigned first_page;
static int changed;

static void *spin (void *arg)
{
  (void) arg;
  while (!stop)
    ;
  return NULL;
}

/* Physical address of the page at &8000 (OS_Memory 0, logical in,
   physical out), as ARMEABISupport reads it; 0 on error.  */
static unsigned page8000 (void)
{
  int block[3] = { 0, 0x8000, 0 };
  if (_swix (OS_Memory, _INR(0,2), (1 << 9) | (1 << 13), block, 1))
    return 0;
  return (unsigned) block[2];
}

/* Does ARMEABISupport find the stack we're on (i.e. this app)?  */
static int found (void)
{
  int handle = 0, here;
  if (_swix (0x59D02 /* ARMEABISupport_StackOp */, _INR(0,1) | _OUT(1),
	     2, &here, &handle))
    return 0;
  return handle != 0;
}

static void note (const char *stage)
{
  unsigned p = page8000 ();
  int f = found ();
  if (first_page == 0)
    first_page = p;
  if (p != first_page || !f)
    changed = 1;
  if (logf)
    {
      fprintf (logf, "  %-24s page at &8000 = &%08X%s, ARMEABISupport %s\n",
	       stage, p, p == first_page ? "" : " (CHANGED)",
	       f ? "finds this program" : "DOESN'T FIND THIS PROGRAM");
      fflush (logf);
    }
}

static void at_exit (void)
{
  note ("atexit (just before exit)");
  if (logf)
    {
      fprintf (logf, "%s\n", changed
	       ? "FAIL: ARMEABISupport will not find this program at exit; "
		 "its record will be left behind"
	       : "PASS: same page throughout; *ARMEABISupport_Info should "
		 "list nothing left by this run");
      fclose (logf);
      logf = NULL;
    }
}

int main (int argc, char **argv)
{
  int wimp = 1, threads = 1, closedown = 1, join = 1, secs = 3, i, handle = 0;
  pthread_t t[2];
  static int block[64];
  static const int messages[] = { 0 };

  for (i = 1; i < argc; i++)
    if (!strcmp (argv[i], "-nowimp")) wimp = 0;
    else if (!strcmp (argv[i], "-nothreads")) threads = 0;
    else if (!strcmp (argv[i], "-noclose")) closedown = 0;
    else if (!strcmp (argv[i], "-nojoin")) join = 0;
    else if (!strcmp (argv[i], "-s") && i + 1 < argc) secs = atoi (argv[++i]);

  logf = fopen ("/<Wimp$ScrapDir>/EABIExit", "a");
  if (logf)
    fprintf (logf, "eabxtest%s%s%s%s:\n", wimp ? "" : " -nowimp",
	     threads ? "" : " -nothreads", closedown ? "" : " -noclose",
	     join ? "" : " -nojoin");
  atexit (at_exit);
  note ("start");

  if (threads)
    for (i = 0; i < 2; i++)
      pthread_create (&t[i], NULL, spin, NULL);

  if (wimp)
    {
      if (_swix (Wimp_Initialise, _INR(0,3) | _OUT(1), 310, 0x4B534154,
		 "EABIExit", messages, &handle))
	{
	  if (logf)
	    fprintf (logf, "  Wimp_Initialise failed\n");
	  return 1;
	}
      note ("after Wimp_Initialise");
      clock_t end = clock () + secs * CLOCKS_PER_SEC;
      while (clock () < end)
	{
	  int reason;
	  _swix (Wimp_Poll, _INR(0,1) | _OUT(0), 0, block, &reason);
	  if ((reason == 17 || reason == 18) && block[4] == 0)
	    break;
	}
      note ("after polling");
    }

  if (threads && join)
    {
      stop = 1;
      for (i = 0; i < 2; i++)
	pthread_join (t[i], NULL);
      note ("after joining threads");
    }

  if (wimp && closedown)
    {
      _swix (Wimp_CloseDown, _INR(0,1), handle, 0x4B534154);
      note ("after Wimp_CloseDown");
    }
  exit (0);
}
