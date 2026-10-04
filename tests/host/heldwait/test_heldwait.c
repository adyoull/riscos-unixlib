/* Host test for __pthread_held_wait: never yields with switching held
   off, releases exactly one level, puts back the hold and its return
   address, refuses when the hold is nested, and tests for cancellation
   only when asked and with the hold released.  */
#include <stdio.h>
#include <internal/unix.h>
#include <pthread.h>

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
  printf ("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static struct __pthread_callevery_block rma;
struct ul_global __ul_global = { 1, &rma, NULL };
#define held (rma.pthread_worksemaphore)

static int yields, fatal, bad_enable, cancels, cancel_held;
int __pthread_disable_ints (void) { held++; return 0; }
int __pthread_enable_ints (void)
{ if (held <= 0) bad_enable++; else held--; return 0; }
void pthread_yield (void)
{
  yields++;
  if (held != 0)
    fatal++;			/* the real one is a fatal error */
  /* Another thread's PTHREAD_UNSAFE call replaces the return address.  */
  __ul_global.pthread_return_address = (void *) 0xDEAD;
}
void pthread_testcancel (void)
{ cancels++; if (held != 0) cancel_held++; }

static void reset (int h)
{
  held = h; yields = fatal = bad_enable = cancels = cancel_held = 0;
  __ul_global.pthread_return_address = (void *) 0x8000;
}

int main (void)
{
  /* Hold 0: plain yield.  */
  reset (0);
  CHECK (__pthread_held_wait (0) == 1);
  CHECK (yields == 1 && !fatal && held == 0 && cancels == 0);

  /* Hold 1 (write ()'s or read ()'s own): released around the yield.  */
  reset (1);
  CHECK (__pthread_held_wait (0) == 1);
  CHECK (yields == 1 && !fatal && !bad_enable && held == 1);
  CHECK (__ul_global.pthread_return_address == (void *) 0x8000);

  /* Hold 2 (fwrite -> write): must not yield, must not change the hold.  */
  reset (2);
  CHECK (__pthread_held_wait (0) == 0);
  CHECK (yields == 0 && held == 2 && !bad_enable);
  reset (3);
  CHECK (__pthread_held_wait (1) == 0 && yields == 0 && cancels == 0
	 && held == 3);

  /* Cancellation point only when asked, and with the hold released.  */
  reset (1);
  CHECK (__pthread_held_wait (1) == 1);
  CHECK (cancels == 1 && !cancel_held && held == 1);
  reset (0);
  CHECK (__pthread_held_wait (1) == 1 && cancels == 1 && !cancel_held);

  /* Threads not running: nothing to yield to.  */
  __ul_global.pthread_system_running = 0;
  reset (0);
  CHECK (__pthread_held_wait (1) == 1 && yields == 0 && cancels == 0);
  reset (1);
  CHECK (__pthread_held_wait (0) == 1 && yields == 0 && held == 1);

  printf ("heldwait: %d checks, %d failed\n", checks, failures);
  return failures != 0;
}
