/* mmap64 ()
   Copyright (c) 2026 UnixLib Developers.

   2026: mmap with a 64-bit offset, for programs built with
   _FILE_OFFSET_BITS=64 (<sys/mman.h> maps mmap to this).  Before, their
   64-bit offset was passed to the 32-bit mmap, which read the wrong
   argument.  EABI: offsets up to 4GB-1 (the largest RISC OS file).
   Otherwise they must fit in 32 bits signed, as for mmap.  */

#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>

#ifdef __ARM_EABI__
extern void *__mmap_offset32 (void *addr, size_t len, int prot, int flags,
			      int fd, unsigned long offset);
#endif

void *
mmap64 (void *addr, size_t len, int prot, int flags, int fd,
	__off64_t offset)
{
#ifdef __ARM_EABI__
  if (offset < 0)
    {
      errno = EINVAL;
      return MAP_FAILED;
    }
  if (offset > (__off64_t) 0xFFFFFFFFul)
    {
      errno = EOVERFLOW;
      return MAP_FAILED;
    }
  return __mmap_offset32 (addr, len, prot, flags, fd, (unsigned long) offset);
#else
  if (offset != (__off64_t) (__off_t) offset)
    {
      errno = EOVERFLOW;
      return MAP_FAILED;
    }
  return mmap (addr, len, prot, flags, fd, (__off_t) offset);
#endif
}
