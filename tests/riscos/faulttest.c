/* faulttest: the exception handlers (riscos-unixlib).

   1. An undefined instruction with a SIGILL handler installed: the handler
      runs and siglongjmps back. "caught SIGILL" must be printed.
   2. The same with no handler: UnixLib must print "Fatal signal received:
      Illegal instruction" (and a backtrace) and the program must exit with
      an error, not hang or take RISC OS down.
   The RISC OS for Linux change (pvops) touches these handlers
   (signal/_signal.s); on a Pi they must behave as before.  */
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>

static sigjmp_buf back;

static void
handler (int sig)
{
  siglongjmp (back, sig);
}

static void
undefined (void)
{
  __asm__ volatile (".word 0xe7f000f0");	/* permanently undefined */
}

int
main (void)
{
  signal (SIGILL, handler);
  if (sigsetjmp (back, 1) == 0)
    {
      undefined ();
      printf ("FAIL: no SIGILL\n");
      return 1;
    }
  printf ("caught SIGILL: ok\n");

  signal (SIGILL, SIG_DFL);
  printf ("now expect \"Fatal signal received: Illegal instruction\"\n");
  fflush (stdout);
  undefined ();
  printf ("FAIL: still running\n");
  return 1;
}
