/* tickertest: two busy threads in a Wimp task while other tasks run.

   UnixLib switches threads with an OS_CallEvery ticker.  Its handler used
   to be in the program's own memory, so if the ticker fired while another
   task was paged in, that task crashed ("abort on instruction fetch" in
   Organizer, seen with Warzone 2100).  The handler now runs from RMA.

     tickertest [-early] [-s secs]
   -early  start the threads before Wimp_Initialise.  UnixLib then can't
           register the Wimp filters that switch the ticker off while this
           task is swapped out, so the ticker keeps running while other
           tasks have the processor: the worst case.
   Have some other desktop programs running (Alarm, a clock, Organizer).
   Results go to <Wimp$ScrapDir>.tickertest and an error box at the end. */
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
  int early = 0, secs = 20, i, handle = 0, h0;
  pthread_t t[2];
  FILE *log = fopen ("/<Wimp$ScrapDir>/tickertest", "w");
  static int block[64];
  static const int messages[] = { 0 };
  char msg[256];

  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-early")) early = 1;
      else if (!strcmp (argv[i], "-s") && i + 1 < argc) secs = atoi (argv[++i]);
    }
  if (!log) log = stderr;

  /* What UnixLib sees at start-up (it caches this as the task handle).  */
  h0 = sysinfo (3) ? sysinfo (5) : 0;
  fprintf (log, "task handle at start-up (Wimp_ReadSysInfo 5): %#x\n", h0);

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
	   (h0 & 0xffff) == (handle & 0xffff) ? "same" : "DIFFERENT");
  if (!early)
    for (i = 0; i < 2; i++)
      pthread_create (&t[i], NULL, spin, (void *) (long) i);
  fprintf (log, "threads started %s Wimp_Initialise; polling for %d s\n",
	   early ? "before" : "after", secs);
  fflush (log);

  /* Poll with null events on, so the other tasks get the processor.  */
  {
    clock_t end = clock () + secs * CLOCKS_PER_SEC;
    unsigned long polls = 0;
    int reason;
    while (clock () < end)
      {
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
	      "PASS: %lu polls, threads counted %lu and %lu. "
	      "Did any other program crash?", polls, count[0], count[1]);
  }
  fprintf (log, "%s\n", msg);
  if (log != stderr) fclose (log);

  block[0] = 0;
  strcpy ((char *) &block[1], msg);
  _swix (Wimp_ReportError, _INR(0,2), block, 1, "TickerTest");
  _swix (Wimp_CloseDown, _INR(0,1), handle, 0x4B534154);
  return 0;
}
