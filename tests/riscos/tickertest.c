/* tickertest: two busy threads in a Wimp task while other tasks run.

   UnixLib switches threads with an OS_CallEvery ticker.  Its handler used
   to be in the program's own memory, so if the ticker fired while another
   task was paged in, that task crashed ("abort on instruction fetch" in
   Organizer, seen with Warzone 2100).  The handler now runs from RMA.

     tickertest [-early] [-starttask] [-s secs]
   -early      start the threads before Wimp_Initialise.  UnixLib then
               can't register the Wimp filters that switch the ticker off
               while this task is swapped out (until it notices the task
               handle), so the ticker keeps running while other tasks have
               the processor: the worst case.
   -starttask  every 5 s, start a child task with Wimp_StartTask (the
               TickerChild Obey file: this program with -child, which
               busy-waits 1 s).  The child runs while we're paged out, but
               we never called Wimp_Poll, so the pre-filter isn't called.
   -child      used by -starttask.
   Have some other desktop programs running (Alarm, a clock, Organizer).
   Results go to <Wimp$ScrapDir>.tickertest and an error box at the end.
   With UnixLib$TickerStats set (the Obey files set it), UnixLib appends
   its ticker counters to that file at exit (docs/THREAD-TICKER.md).  */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <kernel.h>
#include <swis.h>

static volatile int stop;
static volatile unsigned long count[2];

static void *spin (void *arg)
{
  int i = (int) (long) arg;
  while (!stop)
    count[i]++;
  return NULL;
}

static int sysinfo (int n)
{
  int r = 0;
  if (_swix (Wimp_ReadSysInfo, _IN(0) | _OUT(0), n, &r))
    return -1;
  return r;
}

int main (int argc, char **argv)
{
  int early = 0, starttask = 0, secs = 20, i, handle = 0, h0, v0 = 0;
  unsigned long children = 0;
  pthread_t t[2];
  FILE *log;
  static int block[64];
  static const int messages[] = { 0 };
  char msg[256];

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-child"))
	{
	  /* Busy for 1 s, no threads, no Wimp_Initialise, no log.  */
	  clock_t end = clock () + CLOCKS_PER_SEC;
	  while (clock () < end)
	    ;
	  return 0;
	}
      else if (!strcmp (argv[i], "-early")) early = 1;
      else if (!strcmp (argv[i], "-starttask")) starttask = 1;
      else if (!strcmp (argv[i], "-s") && i + 1 < argc) secs = atoi (argv[++i]);
    }
  log = fopen ("/<Wimp$ScrapDir>/tickertest", "w");
  if (!log) log = stderr;

  /* What UnixLib sees at start-up (it caches this as the task handle).  */
  h0 = 0;
  if (sysinfo (3))
    _swix (Wimp_ReadSysInfo, _IN(0) | _OUTR(0,1), 5, &h0, &v0);
  fprintf (log, "task handle at start-up (Wimp_ReadSysInfo 5): %#x, "
	   "Wimp version %d\n", h0, v0);

  if (early)
    for (i = 0; i < 2; i++)
      pthread_create (&t[i], NULL, spin, (void *) (long) i);

  if (_swix (Wimp_Initialise, _INR(0,3) | _OUT(1), 310, 0x4B534154,
	     "TickerTest", messages, &handle))
    {
      fprintf (log, "Wimp_Initialise failed\n");
      return 1;
    }
  fprintf (log, "task handle from Wimp_Initialise:          %#x (%s)\n", handle,
	   h0 == handle ? "same"
	   : (h0 & 0xffff) == (handle & 0xffff) ? "same low 16 bits"
	   : "DIFFERENT");
  if (!early)
    for (i = 0; i < 2; i++)
      pthread_create (&t[i], NULL, spin, (void *) (long) i);
  fprintf (log, "threads started %s Wimp_Initialise; polling for %d s\n",
	   early ? "before" : "after", secs);
  fflush (log);

  /* Poll with null events on, so the other tasks get the processor.  */
  {
    clock_t end = clock () + secs * CLOCKS_PER_SEC;
    clock_t next_child = clock () + 5 * CLOCKS_PER_SEC;
    unsigned long polls = 0;
    int reason;
    while (clock () < end)
      {
	if (starttask && clock () >= next_child)
	  {
	    if (!_swix (Wimp_StartTask, _IN(0), "Run <UST$Dir>.TickerChild"))
	      children++;
	    next_child = clock () + 5 * CLOCKS_PER_SEC;
	  }
	_swix (Wimp_Poll, _INR(0,1) | _OUT(0), 0, block, &reason);
	polls++;
	if (reason == 17 || reason == 18)
	  if (block[4] == 0)	/* Message_Quit */
	    break;
      }
    stop = 1;
    for (i = 0; i < 2; i++)
      pthread_join (t[i], NULL);
    snprintf (msg, sizeof msg,
	      "PASS: %lu polls, %lu child tasks, threads counted %lu and %lu. "
	      "Did any other program crash?", polls, children,
	      count[0], count[1]);
  }
  fprintf (log, "%s\n", msg);
  if (log != stderr) fclose (log);

  block[0] = 0;
  strcpy ((char *) &block[1], msg);
  _swix (Wimp_ReportError, _INR(0,2), block, 1, "TickerTest");
  _swix (Wimp_CloseDown, _INR(0,1), handle, 0x4B534154);
  return 0;
}
