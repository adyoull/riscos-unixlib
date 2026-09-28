/* Truncate a file to a specified length.
   Copyright (c) 2005-2021 UnixLib Developers.  */

#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#ifdef __TARGET_SCL__
# include <stdio.h>
#endif

#include <internal/os.h>
#ifndef __TARGET_SCL__
# include <internal/dev.h>
# include <internal/unix.h>
# include <unixlib/local.h>
# include <internal/fd.h>
# include <pthread.h>
#endif

int
ftruncate (int fd, off_t length)
{
#ifndef __TARGET_SCL__
  PTHREAD_UNSAFE

#ifdef __ARM_EABI__
  /* This is currently a NOOP for a shared memory object.  */
  if (IS_SHM_FD(fd))
    return 0;
#endif

  if (BADF (fd))
    return __set_errno (EBADF);

  struct __unixlib_fd *file_desc = getfd (fd);
  if (file_desc->devicehandle->type != DEV_RISCOS)
    return __set_errno (EOPNOTSUPP);
  int fh_ro = (int) file_desc->devicehandle->handle;
#else
  const FILE *fp = &__iob[fd];
  int fh_ro = fp->__file;
#endif

  const _kernel_oserror *err;
  if ((err = SWI_OS_Args_SetExtent (fh_ro, length)) != NULL)
    return __ul_seterr (err, EOPSYS);

  return 0;
}

int
truncate (const char *ux_file, off_t length)
{
  if (ux_file == NULL)
    return __set_errno (EINVAL);

#ifdef __TARGET_SCL__
  const char *file = ux_file;
#else
  char file[_POSIX_PATH_MAX];
  if (!__riscosify_std (ux_file, 0, file, sizeof (file), NULL))
    return __set_errno (ENAMETOOLONG);
#endif

  /* Open the file, truncate and close it.  */
  unsigned fd;
  const _kernel_oserror *err;
  if ((err = SWI_OS_Find_Open (0xc0, file, &fd)) != NULL
      || (err = SWI_OS_Args_SetExtent (fd, length)) != NULL
      || (err = SWI_OS_Find_Close (fd)) != NULL)
    return __ul_seterr (err, EOPSYS);

  return 0;
}

/* 2026: 64-bit lengths.  RISC OS extents are unsigned 32-bit values, so a
   file can be up to 4GB-1 bytes; ftruncate and truncate pass their 32-bit
   length straight to OS_Args 3, so give them the same bits.  */
int
ftruncate64 (int fd, __off64_t length)
{
  if (length < 0)
    return __set_errno (EINVAL);
  if (length > 0xFFFFFFFFLL)
    return __set_errno (EFBIG);
  return ftruncate (fd, (__off_t) (unsigned long) length);
}

int
truncate64 (const char *ux_file, __off64_t length)
{
  if (length < 0)
    return __set_errno (EINVAL);
  if (length > 0xFFFFFFFFLL)
    return __set_errno (EFBIG);
  return truncate (ux_file, (__off_t) (unsigned long) length);
}
