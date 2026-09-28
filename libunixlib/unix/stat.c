/* UnixLib stat()/stat64() implementation.
   Copyright (c) 2000-2008 UnixLib Developers.  */

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
stat (const char *filename, struct stat *buf)
{
  PTHREAD_UNSAFE

#ifdef DEBUG
  debug_printf ("stat(file=%s)\n", filename);
#endif

  /* Perform a special check for devices.  */
  buf->st_dev = __getdevtype (filename, __get_riscosify_control());

  /* Perform the device specific open operation.  */
  return dev_funcall (buf->st_dev, stat, (filename, buf));
}


/* 2026: the symbol stat64 keeps the pre-5.0.2 layout (a 32-bit st_size,
   the same as struct stat) for objects compiled with older headers, such
   as libstdc++.  New code calls __unixlib_stat64 (unix/stat64.c).  */
int __stat64_compat (const char *filename, struct stat *buf)
  __asm__ ("stat64");

int
__stat64_compat (const char *filename, struct stat *buf)
{
  return stat (filename, buf);
}

