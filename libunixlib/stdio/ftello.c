/* UnixLib ftello(), ftello64() implementation.
   Copyright 2001-2011 UnixLib Developers.  */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <pthread.h>
#include <internal/unix.h>

/* #define DEBUG */
#ifdef DEBUG
#  include <sys/debug.h>
#endif

__off_t
ftello (FILE *stream)
{
  __off_t pos;
  if (fgetpos (stream, &pos) == -1)
    return (__off_t)-1;

  return pos;
}

/* 2026: RISC OS file positions go up to 4GB-1 (unsigned 32-bit); the
   stream keeps them in a 32-bit __off_t, so read them back as unsigned.  */
__off64_t
ftello64 (FILE *stream)
{
  __off64_t pos;
  if (fgetpos64 (stream, &pos) == -1)
    return (__off64_t)-1;

  return pos;
}

