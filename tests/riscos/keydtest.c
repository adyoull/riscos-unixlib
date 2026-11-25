/* keydtest: thread-specific data destructors at thread exit while other
   threads use malloc (riscos-unixlib K13, from riscos-unixlib-web W46:
   pthread_exit freed each key node with switching on, so a thread in
   malloc or free at the same moment could find the heap half-changed).

   For about 10 s: 4 threads allocate and free blocks of many sizes
   without stopping, checking a pattern in each block, while the main
   thread starts short-lived threads that each set 8 keys to malloc'd
   blocks and return, so 8 destructors run and 8 key nodes are freed in
   every pthread_exit.  Every destructor must run exactly once.  The
   heap must stay intact (the patterns, and a final malloc and free of
   every size).  Must print PASS.  */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NKEYS 8
#define NWORK 4

static pthread_key_t keys[NKEYS];
static volatile int stop;
static volatile unsigned long destructed, set, bad;
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

static void
destructor (void *p)
{
  unsigned char *b = p;
  if (b[0] != 0xA5 || b[63] != 0x5A)
    bad++;
  free (p);
  pthread_mutex_lock (&m);
  destructed++;
  pthread_mutex_unlock (&m);
}

static void *
shortlived (void *arg)
{
  (void) arg;
  for (int i = 0; i < NKEYS; i++)
    {
      unsigned char *b = malloc (64);
      if (b == NULL)
	continue;
      memset (b, 0, 64);
      b[0] = 0xA5;
      b[63] = 0x5A;
      pthread_setspecific (keys[i], b);
      pthread_mutex_lock (&m);
      set++;
      pthread_mutex_unlock (&m);
    }
  return NULL;
}

static void *
worker (void *arg)
{
  unsigned seed = (unsigned) (size_t) arg;
  void *blocks[32] = { 0 };
  size_t sizes[32] = { 0 };
  while (!stop)
    {
      seed = seed * 1103515245u + 12345u;
      int i = (seed >> 16) % 32;
      if (blocks[i])
	{
	  unsigned char *b = blocks[i];
	  for (size_t j = 0; j < sizes[i]; j++)
	    if (b[j] != (unsigned char) (j + i))
	      {
		bad++;
		break;
	      }
	  free (b);
	  blocks[i] = NULL;
	}
      else
	{
	  sizes[i] = 8 + (seed >> 8) % 3000;
	  unsigned char *b = malloc (sizes[i]);
	  if (b)
	    for (size_t j = 0; j < sizes[i]; j++)
	      b[j] = (unsigned char) (j + i);
	  blocks[i] = b;
	}
    }
  for (int i = 0; i < 32; i++)
    free (blocks[i]);
  return NULL;
}

int
main (void)
{
  for (int i = 0; i < NKEYS; i++)
    pthread_key_create (&keys[i], destructor);

  pthread_t w[NWORK];
  for (int i = 0; i < NWORK; i++)
    pthread_create (&w[i], NULL, worker, (void *) (size_t) (i + 1));

  time_t end = time (NULL) + 10;
  unsigned long threads = 0;
  while (time (NULL) < end)
    {
      pthread_t t;
      if (pthread_create (&t, NULL, shortlived, NULL) == 0)
	{
	  pthread_join (t, NULL);
	  threads++;
	}
    }
  stop = 1;
  for (int i = 0; i < NWORK; i++)
    pthread_join (w[i], NULL);

  /* The heap still works for every size.  */
  for (size_t n = 1; n < 100000; n += n / 3 + 1)
    {
      void *p = malloc (n);
      if (p == NULL)
	bad++;
      else
	memset (p, 0x33, n);
      free (p);
    }

  printf ("%lu short-lived threads, %lu values set, %lu destructors run, "
	  "%lu damaged blocks\n", threads, set, destructed, bad);
  int ok = threads > 0 && set == destructed && bad == 0;
  printf ("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
