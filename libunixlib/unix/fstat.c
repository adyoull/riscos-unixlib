/* fstat (), fstat64 ()
 * Copyright (c) 2000-2013 UnixLib Developers
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

#include <internal/dev.h>
#include <internal/os.h>
#include <internal/unix.h>
#include <unixlib/local.h>
#include <internal/swiparams.h>
#include <pthread.h>

int
fstat (int fd, struct stat *buf)
{
  PTHREAD_UNSAFE

  if (buf == NULL)
    return __set_errno (EINVAL);

  if (BADF (fd))
    return __set_errno (EBADF);

  const struct __unixlib_fd *file_desc = getfd (fd);

  buf->st_dev = file_desc->devicehandle->type;

  /* Perform the device specific open operation.  */
  return dev_funcall (file_desc->devicehandle->type, fstat, (fd, buf));
}

/* 2026: the symbol fstat64 keeps the pre-5.0.2 layout (a 32-bit st_size,
   the same as struct stat) for objects compiled with older headers, such
   as libstdc++.  New code calls __unixlib_fstat64.  */
int __fstat64_compat (int fd, struct stat *buf) __asm__ ("fstat64");

int
__fstat64_compat (int fd, struct stat *buf)
{
  return fstat (fd, buf);
}

/* struct stat64: a 64-bit st_size.  RISC OS file sizes are unsigned 32-bit
   values, which struct stat holds as a (possibly negative) __off_t.  */
int
__unixlib_fstat64 (int fd, struct stat64 *buf)
{
  struct stat st;

  if (fstat (fd, &st) != 0)
    return -1;
  __stat_to_stat64 (&st, buf);
  return 0;
}
