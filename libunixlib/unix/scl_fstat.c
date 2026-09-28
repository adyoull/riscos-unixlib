/* fstat() for SCL.
   Copyright (c) 2012 UnixLib Developers.  */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <internal/local.h>
#include <internal/os.h>

int
fstat (int fd, struct stat *buf)
{
  const FILE *fp = &__iob[fd];

  const char *buffer = __canonicalise_handle (fp->__file);
  if (buffer == NULL)
    return __set_errno (EBADF);

  /* Get vital file statistics.  */
  unsigned objtype, loadaddr, execaddr, objlen, attr;
  const _kernel_oserror *err = SWI_OS_File_ReadCatInfo (buffer, &objtype,
							&loadaddr, &execaddr,
							&objlen, &attr);
  free ((void *)buffer);
  if (err)
    return __ul_seterr (err, EIO);

  /* OS_File ReadCatInfo returns the allocated size of the file, but we want
     the current extent of the file */
  if ((err = SWI_OS_Args_GetExtent (fp->__file, &objlen)) != NULL)
    return __ul_seterr (err, EIO);

  buf->st_ino = 0;
  buf->st_dev = 0;
  return __stat (objtype, loadaddr, execaddr, objlen, attr, buf);
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

