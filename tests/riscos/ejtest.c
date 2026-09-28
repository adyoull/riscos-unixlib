/* ejtest: join a thread from an atexit() handler (as SDL_WaitThread in a
   shutdown function does).  Before the fix UnixLib aborted with
   "Fatal signal received: Aborted" when the program quit. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static pthread_t worker;
static volatile int stop;

static void *work (void *arg)
{
  unsigned long n = 0;
  (void) arg;
  while (!stop) { n++; pthread_yield (); }
  printf ("worker: stopped after %lu loops\n", n);
  return NULL;
}

static void shutdown_handler (void)
{
  printf ("atexit: joining the worker thread...\n");
  stop = 1;
  int r = pthread_join (worker, NULL);
  printf ("atexit: pthread_join returned %d\n", r);
  printf ("PASS if you see this line and no \"Aborted\"\n");
}

int main (void)
{
  if (pthread_create (&worker, NULL, work, NULL) != 0) { perror ("pthread_create"); return 1; }
  atexit (shutdown_handler);
  printf ("main: returning (exit runs the atexit handler)\n");
  return 0;
}
