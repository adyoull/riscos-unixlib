/* Host test for netlib/ul_socketpair.c (N3, from riscos-unixlib-web W8): UnixLib's
   socketpair, built on Linux as ul_socketpair, makes a working loopback
   TCP pair, in both directions; refuses what it can't make.  */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int ul_socketpair (int af, int type, int protocol, int fd[2]);

static int fails;
#define CHECK(c, what) do { if (c) printf ("ok   %s\n", what); \
  else { printf ("FAIL %s (errno %d)\n", what, errno); fails++; } } while (0)

int main (void)
{
  int sv[2];
  char buf[64];

  CHECK (ul_socketpair (AF_INET, SOCK_STREAM, 0, sv) == 0, "AF_INET stream pair made");
  CHECK (write (sv[0], "ping", 4) == 4 && read (sv[1], buf, 4) == 4 && !memcmp (buf, "ping", 4),
	 "data from fd[0] to fd[1]");
  CHECK (write (sv[1], "pong", 4) == 4 && read (sv[0], buf, 4) == 4 && !memcmp (buf, "pong", 4),
	 "data from fd[1] to fd[0]");
  CHECK (!(fcntl (sv[0], F_GETFL) & O_NONBLOCK) && !(fcntl (sv[1], F_GETFL) & O_NONBLOCK),
	 "both ends blocking");
  close (sv[1]);
  CHECK (read (sv[0], buf, sizeof buf) == 0, "closing one end gives end of file at the other");
  close (sv[0]);

  CHECK (ul_socketpair (AF_UNIX, SOCK_STREAM, 0, sv) == 0, "AF_UNIX accepted (loopback TCP)");
  close (sv[0]); close (sv[1]);

  errno = 0;
  CHECK (ul_socketpair (AF_INET, SOCK_DGRAM, 0, sv) == -1 && errno == EOPNOTSUPP,
	 "a datagram pair is refused (EOPNOTSUPP)");
  errno = 0;
  CHECK (ul_socketpair (AF_INET6, SOCK_STREAM, 0, sv) == -1 && errno == EAFNOSUPPORT,
	 "another family is refused (EAFNOSUPPORT)");

  /* Many pairs: no descriptors left behind.  */
  int before = dup (0); close (before);
  for (int i = 0; i < 200; i++)
    {
      if (ul_socketpair (AF_INET, SOCK_STREAM, 0, sv) != 0) break;
      close (sv[0]); close (sv[1]);
    }
  int after = dup (0); close (after);
  CHECK (before == after, "200 pairs made and closed: no descriptors leaked");

  printf ("%s\n", fails ? "FAILED" : "all passed");
  return fails != 0;
}
