/* mmap64 ()
   Copyright (c) 2026 UnixLib Developers.

   2026: mmap with a 64-bit offset, for programs built with
   _FILE_OFFSET_BITS=64 (<sys/mman.h> maps mmap to this).  Before, their
   64-bit offset was passed to the 32-bit mmap, which read the wrong
   argument.  Offsets must fit in 32 bits, as for mmap.  */

#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>

void *
mmap64 (void *addr, size_t len, int prot, int flags, int fd,
	__off64_t offset)
{
  if (offset != (__off64_t) (__off_t) offset)
    {
      errno = EOVERFLOW;
      return MAP_FAILED;
    }
  return mmap (addr, len, prot, flags, fd, (__off_t) offset);
}
