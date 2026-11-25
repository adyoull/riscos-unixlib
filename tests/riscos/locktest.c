/* locktest: the rc9 fast paths (P10 pthread_mutex_lock/unlock, P11
   pthread_getspecific) keep their meaning under thread switching, and
   how long they take.
   - 4 threads each do 100000 lock / read-modify-write / unlock of one
     counter, with a delay inside so that switches land there: the total
     must be exact.
   - Recursive, error-checking and normal mutexes: trylock, EDEADLK,
     EPERM, counts; a waiter blocked on a mutex gets it when it is given
     up (the slow path, mixed with fast ones).
   - 4 threads each set their own values for 24 keys, then read them back
     100000 times in a changing order, and a __thread counter (GCC's
     emulated TLS goes through pthread_getspecific); destructors see each
     thread's own values; a thread made after others ended reads NULL.
   - Timings: a lock/unlock pair, pthread_getspecific, a __thread access,
     strncpy of 64 bytes.
   Must print PASS.  */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int fails, checks;
#define CHECK(c, ...) do { __atomic_add_fetch (&checks, 1, __ATOMIC_RELAXED); \
  if (!(c) && __atomic_fetch_add (&fails, 1, __ATOMIC_RELAXED) < 10) { printf ("WRONG: " __VA_ARGS__); putchar ('\n'); } } while (0)

static double
secs (void)
{
  struct timespec t;
  clock_gettime (CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}

/* 1. A counter under a mutex.  */
static pthread_mutex_t cm = PTHREAD_MUTEX_INITIALIZER;
static volatile long counter;

static void *
count_thread (void *arg)
{
  (void) arg;
  for (int i = 0; i < 100000; i++)
    {
      pthread_mutex_lock (&cm);
      long v = counter;
      for (volatile int d = 0; d < 20; d++)
	;
      counter = v + 1;
      pthread_mutex_unlock (&cm);
    }
  return NULL;
}

/* 2. A waiter.  */
static pthread_mutex_t wm = PTHREAD_MUTEX_INITIALIZER;
static volatile int waiter_got;

static void *
waiter (void *arg)
{
  (void) arg;
  pthread_mutex_lock (&wm);
  waiter_got = 1;
  pthread_mutex_unlock (&wm);
  return NULL;
}

/* 3. Keys.  */
#define NKEYS 24
static pthread_key_t keys[NKEYS];
static __thread long tls_counter;
static volatile int destructed[4][NKEYS];

static void
destructor (void *v)
{
  long x = (long) v;		/* thread * 1000 + key + 1 */
  int t = x / 1000, k = x % 1000 - 1;
  if (t >= 0 && t < 4 && k >= 0 && k < NKEYS)
    destructed[t][k]++;
}

static void *
key_thread (void *arg)
{
  long t = (long) arg;
  /* Set in a scrambled order.  */
  for (int i = 0; i < NKEYS; i++)
    {
      int k = (i * 7 + (int) t) % NKEYS;
      pthread_setspecific (keys[k], (void *) (t * 1000 + k + 1));
    }
  int bad = 0;
  for (int i = 0; i < 100000; i++)
    {
      int k = (i % 5 == 0) ? (i / 5) % NKEYS : (int) (t % NKEYS);
      if ((long) pthread_getspecific (keys[k]) != t * 1000 + k + 1)
	bad++;
      tls_counter++;
    }
  CHECK (bad == 0, "thread %ld read another value %d times", t, bad);
  CHECK (tls_counter == 100000, "thread %ld's __thread counter is %ld", t, tls_counter);
  /* Change one value and read it back.  */
  pthread_setspecific (keys[3], (void *) (t * 1000 + 3 + 1));
  CHECK ((long) pthread_getspecific (keys[3]) == t * 1000 + 4, "thread %ld after a second set", t);
  return NULL;
}

static void *
late_thread (void *arg)
{
  (void) arg;
  for (int k = 0; k < NKEYS; k++)
    CHECK (pthread_getspecific (keys[k]) == NULL, "a new thread reads NULL for key %d", k);
  CHECK (tls_counter == 0, "a new thread's __thread counter is 0");
  return NULL;
}

int
main (void)
{
  pthread_t t[4];

  /* 1.  */
  for (long i = 0; i < 4; i++)
    pthread_create (&t[i], NULL, count_thread, NULL);
  for (int i = 0; i < 4; i++)
    pthread_join (t[i], NULL);
  CHECK (counter == 400000, "counter %ld, not 400000", counter);

  /* 2. Types.  */
  pthread_mutexattr_t a;
  pthread_mutex_t rm, em, nm;
  pthread_mutexattr_init (&a);
  pthread_mutexattr_settype (&a, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init (&rm, &a);
  pthread_mutexattr_settype (&a, PTHREAD_MUTEX_ERRORCHECK);
  pthread_mutex_init (&em, &a);
  pthread_mutex_init (&nm, NULL);
  CHECK (pthread_mutex_lock (&rm) == 0 && pthread_mutex_lock (&rm) == 0
	 && pthread_mutex_trylock (&rm) == 0, "recursive: three locks");
  CHECK (pthread_mutex_unlock (&rm) == 0 && pthread_mutex_unlock (&rm) == 0
	 && pthread_mutex_unlock (&rm) == 0, "recursive: three unlocks");
  CHECK (pthread_mutex_unlock (&rm) == EPERM, "recursive: a fourth unlock is EPERM");
  CHECK (pthread_mutex_lock (&em) == 0, "errorcheck: lock");
  CHECK (pthread_mutex_lock (&em) == EDEADLK, "errorcheck: relock is EDEADLK");
  CHECK (pthread_mutex_trylock (&em) == EBUSY, "errorcheck: trylock is EBUSY");
  CHECK (pthread_mutex_unlock (&em) == 0, "errorcheck: unlock");
  CHECK (pthread_mutex_unlock (&em) == EPERM, "errorcheck: unlock again is EPERM");
  CHECK (pthread_mutex_unlock (&nm) == EPERM, "normal: unlock when not locked is EPERM");
  CHECK (pthread_mutex_trylock (&nm) == 0 && pthread_mutex_unlock (&nm) == 0, "normal: trylock, unlock");
  CHECK (pthread_mutex_lock (NULL) == EINVAL && pthread_mutex_unlock (NULL) == EINVAL, "NULL is EINVAL");
  pthread_mutex_destroy (&nm);
  CHECK (pthread_mutex_lock (&nm) == EINVAL, "destroyed: lock is EINVAL");

  /* A waiter gets the mutex when it is given up.  */
  pthread_t w;
  pthread_mutex_lock (&wm);
  pthread_create (&w, NULL, waiter, NULL);
  for (int i = 0; i < 20; i++)
    pthread_yield ();
  CHECK (waiter_got == 0, "the waiter got a held mutex");
  CHECK (wm.waiting != NULL, "the waiter isn't waiting (the slow path wasn't used)");
  pthread_mutex_unlock (&wm);
  pthread_join (w, NULL);
  CHECK (waiter_got == 1, "the waiter didn't get the mutex");
  CHECK (pthread_mutex_trylock (&wm) == 0 && pthread_mutex_unlock (&wm) == 0, "free again after the waiter");

  /* 3. Keys.  */
  for (int k = 0; k < NKEYS; k++)
    pthread_key_create (&keys[k], destructor);
  for (long i = 0; i < 4; i++)
    pthread_create (&t[i], NULL, key_thread, (void *) i);
  for (int i = 0; i < 4; i++)
    pthread_join (t[i], NULL);
  for (int i = 0; i < 4; i++)
    for (int k = 0; k < NKEYS; k++)
      CHECK (destructed[i][k] == 1, "thread %d key %d: destructor called %d times", i, k, destructed[i][k]);
  pthread_create (&t[0], NULL, late_thread, NULL);
  pthread_join (t[0], NULL);
  CHECK (pthread_getspecific (keys[0]) == NULL, "the main thread reads NULL");

  /* 4. Timings.  */
  pthread_setspecific (keys[0], &a);
  for (int k = 1; k < NKEYS; k++)
    pthread_setspecific (keys[k], &a);
  volatile long sink = 0;
  static char src[80] = "abcdefghijklmnopqrstuvwxyz012345abcdefghijklmnopqrstuvwxyz012345", dst[80];
  char *(*volatile p_strncpy) (char *, const char *, size_t) = strncpy;
  double t0 = secs ();
  for (int i = 0; i < 200000; i++)
    {
      pthread_mutex_lock (&cm);
      pthread_mutex_unlock (&cm);
    }
  double t1 = secs ();
  for (int i = 0; i < 200000; i++)
    sink += (long) pthread_getspecific (keys[0]);	/* the same key: remembered */
  double t2 = secs ();
  for (int i = 0; i < 200000; i++)	/* two keys in turn: never remembered */
    sink += (long) pthread_getspecific (keys[i & 1]);
  double t2b = secs ();
  for (int i = 0; i < 200000; i++)
    sink += ++tls_counter;
  double t3 = secs ();
  for (int i = 0; i < 200000; i++)
    sink += (long) p_strncpy (dst, src, 64);
  double t4 = secs ();
  printf ("%d checks, %d wrong\n", checks, fails);
  printf ("lock+unlock %.0f ns, getspecific %.0f ns (two keys in turn %.0f ns), "
	  "__thread %.0f ns, strncpy 64 %.0f ns\n",
	  (t1 - t0) * 5e3, (t2 - t1) * 5e3, (t2b - t2) * 5e3, (t3 - t2b) * 5e3,
	  (t4 - t3) * 5e3);
  printf ("%s\n", fails ? "FAIL" : "PASS");
  return fails != 0;
}
