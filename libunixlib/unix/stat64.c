/* __unixlib_stat64 (), __unixlib_lstat64 ()
   Copyright (c) 2026 UnixLib Developers.

   2026: struct stat64 with a 64-bit st_size.  RISC OS file sizes are
   unsigned 32-bit values (files up to 4GB-1 bytes); struct stat keeps them
   in a 32-bit __off_t, where sizes of 2GB and over look negative.  The
   64-bit versions read them back as unsigned.  <sys/stat.h> maps stat64,
   lstat64 (and stat, lstat with _FILE_OFFSET_BITS=64) to these; the old
   symbols stat64 and lstat64 keep the old layout (see stat.c).  */

#include <sys/stat.h>

int
__unixlib_stat64 (const char *filename, struct stat64 *buf)
{
  struct stat st;

  if (stat (filename, &st) != 0)
    return -1;
  __stat_to_stat64 (&st, buf);
  return 0;
}

int
__unixlib_lstat64 (const char *filename, struct stat64 *buf)
{
  struct stat st;

  if (lstat (filename, &st) != 0)
    return -1;
  __stat_to_stat64 (&st, buf);
  return 0;
}
