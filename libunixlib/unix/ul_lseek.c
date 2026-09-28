/* lseek (), lseek64 ()
 * Copyright (c) 2000-2011 UnixLib Developers
 */

#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>

#include <internal/dev.h>
#include <internal/unix.h>
#include <internal/fd.h>

__off_t
lseek (int fd, __off_t offset, int whence)
{
  PTHREAD_UNSAFE

  if (BADF (fd))
    return __set_errno (EBADF);

  struct __unixlib_fd *file_desc = getfd (fd);

  /* The validity of whence is check by the device specific operation.  */

  return dev_funcall (file_desc->devicehandle->type, lseek,
		      (file_desc, offset, whence));
}

/* 2026: RISC OS files can be up to 4GB-1 bytes (unsigned 32-bit file
   pointers), so lseek64 reaches that far on RISC OS files.  Before, it
   stopped at 2GB-1 like lseek.  lseek is unchanged.  */
__off64_t
lseek64 (int fd, __off64_t offset, int whence)
{
  PTHREAD_UNSAFE

  if (BADF (fd))
    return __set_errno (EBADF);

  struct __unixlib_fd *file_desc = getfd (fd);

  if (file_desc->devicehandle->type == DEV_RISCOS)
    return __fslseek64 (file_desc, offset, whence);

  /* Other devices: as before.  */
  if (offset != (__off64_t)(__off_t)offset)
    return __set_errno (EOVERFLOW);
  return lseek (fd, (__off_t) offset, whence);
}
