/* lstat (), lstat64 ()
 * Copyright (c) 2000-2009 UnixLib Developers
 */

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>

#include <internal/dev.h>
#include <internal/os.h>
#include <internal/local.h>
#include <internal/swiparams.h>
#include <internal/unix.h>
#include <pthread.h>

/* #define DEBUG */
#ifdef DEBUG
#  include <sys/debug.h>
#endif

int
lstat (const char *filename, struct stat *buf)
{
  PTHREAD_UNSAFE

#ifdef DEBUG
  debug_printf ("lstat(file=%s)\n", filename);
#endif

  /* Perform a special check for devices.  */
  buf->st_dev = __getdevtype (filename, __get_riscosify_control());

  /* Perform the device specific open operation.  */
  return dev_funcall (buf->st_dev, lstat, (filename, buf));
}

/* 2026: the symbol lstat64 keeps the pre-5.0.2 layout (see stat.c).  */
int __lstat64_compat (const char *filename, struct stat *buf)
  __asm__ ("lstat64");

int
__lstat64_compat (const char *filename, struct stat *buf)
{
  return lstat (filename, buf);
}
