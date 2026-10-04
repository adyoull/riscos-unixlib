/* dsstest: /dev/dsp written through stdio from a threaded program
   (riscos-unixlib, 5.0.3.3-rc1; the 2026-10-04 audit).

   fwrite holds thread switching off and write () holds it again, so the
   wait for room in the sound queue is inside a 2-deep hold.  Before
   5.0.3.3-rc1 that wait released one level and called pthread_yield,
   which stopped the program ("pthread_yield called with context
   switching disabled") the first time the queue filled.

   Plays 3 s of a 440 Hz tone with fwrite while a worker thread counts;
   with -d through DigitalRenderer (sets UnixLib$DSP).  Must print PASS
   (and you should hear the tone).  */
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile int stop;
static volatile unsigned long ticks;

static void *work (void *arg)
{
  (void) arg;
  while (!stop)
    {
      ticks++;
      sched_yield ();
    }
  return NULL;
}

int main (int argc, char **argv)
{
  static short buf[1024 * 2];
  pthread_t t;
  FILE *f;
  size_t total = 0;
  int bad = 0;

  if (argc > 1 && strcmp (argv[1], "-d") == 0)
    setenv ("UnixLib$DSP", "DigitalRenderer", 1);
  pthread_create (&t, NULL, work, NULL);
  if ((f = fopen ("/dev/dsp", "wb")) == NULL)
    {
      perror ("/dev/dsp");
      return 1;
    }
  setvbuf (f, NULL, _IOFBF, 8192);
  unsigned long t0 = ticks;
  for (int frame = 0; frame < 44100 * 3; )
    {
      for (int i = 0; i < 1024; i++, frame++)
	buf[2 * i] = buf[2 * i + 1]
	  = (short) (8000 * sin (2 * M_PI * 440 * frame / 44100.0));
      if (fwrite (buf, sizeof buf, 1, f) != 1)
	{
	  printf ("fwrite failed\n");
	  bad = 1;
	  break;
	}
      total += sizeof buf;
    }
  if (fclose (f) != 0)
    bad = 1;
  stop = 1;
  pthread_join (t, NULL);
  printf ("wrote %u bytes through fwrite; worker ran %lu times\n",
	  (unsigned) total, ticks - t0);
  /* With the hold nested the wait spins without yielding, but between
     fwrite calls (and in fclose's wait) the worker runs.  */
  if (ticks == t0)
    {
      printf ("the worker never ran\n");
      bad = 1;
    }
  printf ("%s\n", bad ? "FAIL" : "PASS");
  return bad;
}
