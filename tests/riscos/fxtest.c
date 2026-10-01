/* fxtest: fork/vfork/exec from a threaded program (riscos-unixlib).

   A UnixLib fork/vfork child that exited used to tear down its parent's
   thread ticker (and detach it from the PThreadTicker module), so the
   parent then ran its threads on a freed block: the review of 2026-10-01,
   K5 in MODIFICATIONS.md.  The usual trigger is a threaded program whose
   vfork + exec fails, or that calls system().

   With two busy threads running, this does:
     1. vfork + exec of a program that doesn't exist (on RISC OS the exec
        runs it as a command, which fails, so the child exits non-zero),
     2. system("Echo ..."), a vfork + exec that works,
   three times each, and after every one checks that the threads still
   get the processor.

     fxtest -fork            also fork + _exit (with the threads)
     fxtest -fork -nothreads only fork + _exit, no threads: is fork itself
                             safe?  Then a child that returns from the
                             function that called fork and uses the stack
                             there: the parent's stack must be unchanged
                             (5.0.3.1-rc6).

   If the PThreadTicker module is loaded (run
   LoadTicker first), it also checks that the module still counts this
   program as a user, and at the end that it refuses to be killed
   (OS_Module 4) while this program runs.  Must print PASS.  */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <kernel.h>
#include <swis.h>

static volatile int stop;
static volatile unsigned long count[2];
static int fails;

static void *
spin (void *arg)
{
  int i = (int) (long) arg;
  while (!stop)
    count[i]++;
  return NULL;
}

/* Do both threads get the processor over 30 cs?  */
static int
threads_run (const char *after)
{
  unsigned long c0 = count[0], c1 = count[1];
  clock_t end = clock () + 30;
  while (clock () < end)
    ;
  if (count[0] == c0 || count[1] == c1)
    {
      printf ("FAIL: after %s the threads stopped (%lu, %lu)\n", after,
	      count[0] - c0, count[1] - c1);
      fails++;
      return 0;
    }
  return 1;
}

/* Child: use plenty of stack (after returning from stack_fork, so over
   the parent's stack_fork frame).  */
static int __attribute__ ((noinline))
scribble (void)
{
  volatile unsigned char buf[8192];
  memset ((unsigned char *) buf, 0xEE, sizeof buf);
  return buf[100];
}

/* Fork with a pattern on the stack.  The child returns at once; the
   parent waits for it and then checks the pattern.  *ok: 1 if unchanged,
   0 if not.  */
static pid_t __attribute__ ((noinline))
stack_fork (int *ok)
{
  volatile unsigned char pat[2048];
  int j, status;
  pid_t pid;

  for (j = 0; j < (int) sizeof pat; j++)
    pat[j] = (unsigned char) (j * 7 + 1);
  pid = fork ();
  if (pid <= 0)
    return pid;
  waitpid (pid, &status, 0);
  *ok = WIFEXITED (status) && WEXITSTATUS (status) == 4;
  if (!*ok)
    printf ("FAIL: stack test child status %d\n", status);
  for (j = 0; j < (int) sizeof pat; j++)
    if (pat[j] != (unsigned char) (j * 7 + 1))
      {
	printf ("FAIL: the child changed the parent's stack (byte %d)\n", j);
	*ok = 0;
	break;
      }
  return pid;
}

/* The PThreadTicker module's count of programs using it, or -1 if it isn't
   loaded.  OS_Module 18 gives the module's private word (its workspace
   pointer) in R4; the count is the first word of the workspace.  */
static int
ticker_users (unsigned int *ws_out)
{
  static unsigned int ws = 0;
  if (_swix (OS_Module, _INR(0,1) | _OUT(4), 18, "PThreadTicker", &ws))
    return -1;
  if (ws_out)
    *ws_out = ws;
  if (ws < 0x8000 || (ws & 3))
    return -2;			/* not a workspace pointer */
  return *(volatile int *) ws;
}

static void
step (const char *what)
{
  printf ("  %s\n", what);
  fflush (stdout);
}

int
main (int argc, char **argv)
{
  pthread_t t[2];
  static int i, status, users0, users, do_fork = 0, threads = 1;
  /* static: a vfork child shares (and may change) this stack frame.  */
  static unsigned int ws = 0;
  static pid_t pid;

  for (i = 1; i < argc; i++)
    if (!strcmp (argv[i], "-fork"))
      do_fork = 1;
    else if (!strcmp (argv[i], "-nothreads"))
      threads = 0;

  if (!threads)
    {
      printf ("fxtest: fork without threads\n");
      for (i = 0; i < 3; i++)
	{
	  step ("fork");
	  pid = fork ();
	  if (pid == 0)
	    _exit (3);
	  if (pid < 0)
	    {
	      printf ("fork failed: %s\n", strerror (errno));
	      return 1;
	    }
	  step ("back in the parent; waiting");
	  waitpid (pid, &status, 0);
	  if (!WIFEXITED (status) || WEXITSTATUS (status) != 3)
	    {
	      printf ("FAIL: fork child status %d\n", status);
	      return 1;
	    }
	}
      step ("fork, and the child uses the parent's stack");
      {
	static int ok;
	ok = 0;
	pid = stack_fork (&ok);
	if (pid == 0)
	  {
	    scribble ();
	    _exit (4);
	  }
	if (pid < 0)
	  {
	    printf ("fork failed: %s\n", strerror (errno));
	    return 1;
	  }
	if (!ok)
	  return 1;
      }
      printf ("PASS: fork without threads\n");
      return 0;
    }

  printf ("fxtest: vfork/exec%s from a threaded program\n",
	  do_fork ? " and fork" : "");
  for (i = 0; i < 2; i++)
    if (pthread_create (&t[i], NULL, spin, (void *) (long) i))
      {
	printf ("FAIL: pthread_create\n");
	return 1;
      }
  if (!threads_run ("starting"))
    return 1;

  users0 = ticker_users (&ws);
  if (users0 == -1)
    printf ("PThreadTicker isn't loaded: module checks skipped "
	    "(run LoadTicker first for those)\n");
  else
    printf ("PThreadTicker: workspace &%08X, %d program(s) using it\n",
	    ws, users0);
  if (users0 == -2 || (users0 >= 0 && (users0 < 1 || users0 > 64)))
    {
      printf ("FAIL: the module's count looks wrong (%d): OS_Module 18's "
	      "R4 isn't what UnixLib expects\n", users0);
      fails++;
    }

  for (i = 0; i < 3; i++)
    {
      /* 1. vfork + exec that fails.  */
      step ("vfork + exec of a program that doesn't exist");
      pid = vfork ();
      if (pid == 0)
	{
	  execl ("/fxtest/no/such/program", "nosuch", (char *) NULL);
	  _exit (127);
	}
      if (pid < 0)
	{
	  printf ("FAIL: vfork\n");
	  fails++;
	}
      else
	{
	  waitpid (pid, &status, 0);
	  if (!WIFEXITED (status) || WEXITSTATUS (status) == 0)
	    {
	      printf ("FAIL: vfork child status %d (should be an exit with "
		      "a non-zero code)\n", status);
	      fails++;
	    }
	}
      threads_run ("a vfork whose exec failed");

      /* fork + _exit, only with -fork.  */
      if (do_fork)
	{
	  step ("fork");
	  pid = fork ();
	  if (pid == 0)
	    _exit (3);
	  if (pid < 0)
	    printf ("(fork not available here: %s)\n", strerror (errno));
	  else
	    {
	      waitpid (pid, &status, 0);
	      if (!WIFEXITED (status) || WEXITSTATUS (status) != 3)
		{
		  printf ("FAIL: fork child status %d\n", status);
		  fails++;
		}
	    }
	  threads_run ("a fork child's exit");
	}

      /* 2. system(): vfork + exec that works.  */
      step ("system()");
      if (system ("Echo   (a command run by system)") != 0)
	{
	  printf ("FAIL: system()\n");
	  fails++;
	}
      threads_run ("system()");

      if (users0 >= 1)
	{
	  users = ticker_users (NULL);
	  if (users != users0)
	    {
	      printf ("FAIL: the module's count changed from %d to %d "
		      "(round %d)\n", users0, users, i + 1);
	      fails++;
	    }
	}
    }

  /* The module must refuse to die while this program uses it.  Only tried
     when its count looks right (otherwise killing it could crash us).  */
  if (users0 >= 1 && users0 <= 64 && fails == 0)
    {
      _kernel_oserror *e = _swix (OS_Module, _INR(0,1), 4, "PThreadTicker");
      if (e)
	printf ("RMKill refused: \"%s\" (as it should)\n", e->errmess);
      else
	{
	  printf ("FAIL: PThreadTicker was killed while in use\n");
	  fails++;
	}
      threads_run ("the RMKill attempt");
    }

  stop = 1;
  pthread_join (t[0], NULL);
  pthread_join (t[1], NULL);
  if (fails)
    printf ("FAIL (%d)\n", fails);
  else
    printf ("PASS%s\n", users0 == -1
	    ? " (without the module; run LoadTicker and try again)" : "");
  return fails != 0;
}
