/* Synchronise unwritten data in buffers to disk.
   Copyright (c) 2004-2011 UnixLib Developers.  */

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <internal/os.h>
#include <internal/dev.h>
#include <internal/unix.h>
#include <internal/fd.h>
#include <pthread.h>

/* This function is always successful.  */
void
sync (void)
{
  /* Ensure data has been written to all files on temporary filing
     system.  */
  (void) SWI_OS_Args_Flush (0);
}

int
fsync (int fd)
{
  PTHREAD_UNSAFE_CANCELLATION

  if (BADF (fd))
    return __set_errno (EBADF);

  struct __unixlib_fd *file_desc = getfd (fd);

  /* Only meaningful for those backed by a real RISC OS file handle.  */
  if (file_desc->devicehandle->type != DEV_RISCOS)
    return __set_errno (EINVAL);

  /* 2026: a file open only for reading has nothing to write back, so
     succeed (as glibc and the BSDs do) rather than fail with EBADF.
     Programs that fsync() before close() (PhysFS) otherwise never closed
     read-only files.  */
  if (!(file_desc->fflag & (O_WRONLY | O_RDWR)))
    return 0;

  /* Ensure data has been written to the file.  */
  const _kernel_oserror *err;
  if ((err = SWI_OS_Args_Flush ((int) file_desc->devicehandle->handle)) != NULL)
    return __ul_seterr (err, EOPSYS);

  return 0;
}

/* 2026: RISC OS has nothing separate to flush for metadata.  */
int
fdatasync (int fd)
{
  return fsync (fd);
}
