/* socketpair ()
 * Written by Peter Naulls
 * Copyright (c) 2003-2010 UnixLib Developers
 *
 * 2026 (N3, from riscos-unixlib-web W8): a loopback TCP connection, made so that
 * it works on RISC OS's Internet stack.  It used to connect to the
 * address the listening socket was bound to, 0.0.0.0, from a socket set
 * non-blocking, and gave up when connect () failed, which a non-blocking
 * connect normally does (EINPROGRESS) until the other end accepts.  The
 * web browser's first run on a Pi 4 stopped there (2026-10-22).  Now it
 * connects to 127.0.0.1, accepts, then waits (up to 5 s) for the
 * client's side to finish; the review (2026-11-22) added a 5 s limit on
 * the accept, a check that the accepted connection is our own client, and
 * TCP_NODELAY on both ends.  AF_UNIX is accepted too, for portable code:
 * RISC OS has no Unix domain sockets, so it gets loopback TCP as well.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
socketpair (int af, int type, int protocol, int fd[2])
{
  (void) protocol;

  /* Only a stream (TCP) can be made.  */
  if (type != SOCK_STREAM)
    return __set_errno (EOPNOTSUPP);
  if (af != AF_INET && af != AF_UNIX)
    return __set_errno (EAFNOSUPPORT);

  int listener = -1, client = -1, server = -1, save_errno;
  struct sockaddr_in addr;
  socklen_t len = sizeof (addr);

  memset (&addr, 0, sizeof (addr));
  addr.sin_family = AF_INET;
  addr.sin_port = 0;			/* any free port */
  addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

  if ((listener = socket (AF_INET, SOCK_STREAM, 0)) < 0
      || bind (listener, (const struct sockaddr *) &addr, sizeof (addr)) < 0
      || getsockname (listener, (struct sockaddr *) &addr, &len) < 0
      || listen (listener, 1) < 0
      || (client = socket (AF_INET, SOCK_STREAM, 0)) < 0)
    goto fail;

  /* Start connecting without waiting: the accept () completes it.  */
  int flags = fcntl (client, F_GETFL, 0);
  if (flags == -1 || fcntl (client, F_SETFL, flags | O_NONBLOCK) == -1)
    goto fail;
  if (connect (client, (const struct sockaddr *) &addr, sizeof (addr)) < 0
      && errno != EINPROGRESS && errno != EALREADY)
    goto fail;

  /* The connection to accept: wait for it (5 s at most), so a connect
     that never arrives can't block for ever.  */
  fd_set rset;
  struct timeval tv = { 5, 0 };
  FD_ZERO (&rset);
  FD_SET (listener, &rset);
  int n = select (listener + 1, &rset, NULL, NULL, &tv);
  if (n <= 0)
    {
      if (n == 0)
	errno = ETIMEDOUT;
      goto fail;
    }
  struct sockaddr_in peer, mine;
  socklen_t peer_len = sizeof (peer), mine_len = sizeof (mine);
  if ((server = accept (listener, (struct sockaddr *) &peer, &peer_len)) < 0)
    goto fail;

  /* The client's side is connected once it's writable; then check that
     it connected rather than failed.  */
  fd_set wset;
  tv.tv_sec = 5;
  tv.tv_usec = 0;
  FD_ZERO (&wset);
  FD_SET (client, &wset);
  n = select (client + 1, NULL, &wset, NULL, &tv);
  if (n <= 0)
    {
      if (n == 0)
	errno = ETIMEDOUT;
      goto fail;
    }
  int err = 0;
  socklen_t err_len = sizeof (err);
  if (getsockopt (client, SOL_SOCKET, SO_ERROR, &err, &err_len) < 0)
    goto fail;
  if (err != 0)
    {
      errno = err;
      goto fail;
    }
  /* The accepted connection must be our own client's, not another
     program's (compared by port, once the client is connected).  */
  if (getsockname (client, (struct sockaddr *) &mine, &mine_len) < 0)
    goto fail;
  if (peer.sin_port != mine.sin_port)
    {
      errno = ECONNREFUSED;
      goto fail;
    }

  /* Both ends blocking, as a socket pair starts, and no Nagle delay: a
     pair is mostly used for small messages and one-byte wake-ups.  */
  fcntl (client, F_SETFL, flags);
  int one = 1;
  (void) setsockopt (client, IPPROTO_TCP, TCP_NODELAY, &one, sizeof (one));
  (void) setsockopt (server, IPPROTO_TCP, TCP_NODELAY, &one, sizeof (one));
  close (listener);
  fd[0] = server;
  fd[1] = client;
  return 0;

fail:
  save_errno = errno;
  if (server >= 0)
    close (server);
  if (client >= 0)
    close (client);
  if (listener >= 0)
    close (listener);
  return __set_errno (save_errno);
}
