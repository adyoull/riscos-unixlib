/* isatty ()
 * Copyright (c) 2000-2008 UnixLib Developers
 */

#include <errno.h>
#include <unistd.h>

#include <internal/dev.h>
#include <internal/unix.h>
#include <internal/fd.h>
#include <pthread.h>

/* Return 1 if fd is a tty.  */
int
isatty (int fd)
{
  PTHREAD_UNSAFE

  /* 2026: not a terminal: 0 (it was -1, which callers took as true),
     with errno EBADF if the fd isn't open, as POSIX has it.  */
  if (BADF (fd))
#ifdef __ARM_EABI__
    /* Perhaps one of ARMEABISupport's fds (shared memory, shared files): sys/mman-armeabi.c.  */
    return __armeabi_fd_isatty (fd);
#else
    {
      (void) __set_errno (EBADF);
      return 0;
    }
#endif

  return getfd (fd)->devicehandle->type == DEV_TTY;
}
