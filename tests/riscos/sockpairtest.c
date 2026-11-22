/* sockpairtest: socketpair on RISC OS's Internet stack (riscos-unixlib N3,
   from riscos-unixlib-web W8), and mkdtemp (H3).  Needs the Internet
   module (loopback).  Must print PASS.  */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>

static int bad;

static void
ok (int cond, const char *what)
{
  printf ("%-56s %s\n", what, cond ? "ok" : "WRONG");
  if (!cond)
    bad = 1;
}

static double
now (void)
{
  struct timeval tv;
  gettimeofday (&tv, NULL);
  return tv.tv_sec + tv.tv_usec / 1e6;
}

int
main (void)
{
  int fd[2];
  int r = socketpair (AF_INET, SOCK_STREAM, 0, fd);
  ok (r == 0, "socketpair (AF_INET, SOCK_STREAM)");
  if (r != 0)
    printf ("  errno %d %s\n", errno, strerror (errno));
  else
    {
      char buf[16] = { 0 };
      ok (write (fd[0], "ping", 4) == 4 && read (fd[1], buf, 4) == 4
	  && memcmp (buf, "ping", 4) == 0, "one way");
      ok (write (fd[1], "pong", 4) == 4 && read (fd[0], buf, 4) == 4
	  && memcmp (buf, "pong", 4) == 0, "and back");
      /* A one-byte wake-up, 200 times there and back: no Nagle delay.  */
      double t = now ();
      int good = 1;
      for (int i = 0; i < 200 && good; i++)
	good = write (fd[0], "x", 1) == 1 && read (fd[1], buf, 1) == 1
	       && write (fd[1], "y", 1) == 1 && read (fd[0], buf, 1) == 1;
      t = now () - t;
      printf ("  200 one-byte round trips: %.3f s\n", t);
      ok (good && t < 5.0, "one-byte round trips, no long delays");
      close (fd[0]);
      close (fd[1]);
    }
  ok (socketpair (AF_UNIX, SOCK_STREAM, 0, fd) == 0, "socketpair (AF_UNIX): loopback TCP");
  close (fd[0]);
  close (fd[1]);
  ok (socketpair (AF_INET, SOCK_DGRAM, 0, fd) == -1 && errno == EOPNOTSUPP,
      "SOCK_DGRAM refused (EOPNOTSUPP)");
  int made = 0;
  for (int i = 0; i < 50; i++)
    if (socketpair (AF_INET, SOCK_STREAM, 0, fd) == 0)
      {
	made++;
	close (fd[0]);
	close (fd[1]);
      }
  ok (made == 50, "50 pairs made and closed");

  char tmpl[] = "/<Wimp$ScrapDir>/dtXXXXXX";
  char *d = mkdtemp (tmpl);
  struct stat st;
  ok (d != NULL && stat (d, &st) == 0 && S_ISDIR (st.st_mode),
      "mkdtemp makes a directory");
  if (d)
    {
      printf ("  %s\n", d);
      rmdir (d);
    }
  char badt[] = "/<Wimp$ScrapDir>/dtXXXX";
  ok (mkdtemp (badt) == NULL && errno == EINVAL, "mkdtemp: a bad template is EINVAL");

  printf ("%s\n", bad ? "FAIL" : "PASS");
  return bad;
}
