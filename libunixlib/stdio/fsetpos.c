/* UnixLib fsetpos(), fsetpos64() implementation.
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


int
fsetpos (FILE *stream, const __off_t *pos)
{
  PTHREAD_UNSAFE

  if (!__validfp (stream) || pos == NULL)
    return __set_errno (EINVAL);

  /* Write out any pending data.  */
  if (stream->__mode.__bits.__write && __flsbuf (EOF, stream) == EOF)
    return -1;

  __off_t cur_offset = lseek (fileno (stream), *pos, SEEK_SET);
  if (cur_offset == (__off_t)-1)
    {
      stream->__error = 1;
      return -1;
    }

  stream->__offset = cur_offset;

#ifdef DEBUG
  debug_printf ("-- fsetpos(fd=%d): newpos=%u\n", stream->fd, stream->__offset);
#endif

  /* Set the input counter to zero so we will read in
     fresh data upon the next read.  */
  stream->i_cnt = 0;
  /* Discard the pushed back character.  */
  stream->__pushedback = 0;
  /* Since we are moving the file position, we are no longer at EOF.  */
  stream->__eof = 0;

  return 0;
}
/* 2026: positions up to 4GB-1 (fseeko64).  The old version converted
   the pointer POS, not the position, so it moved to a meaningless place.  */
int
fsetpos64 (FILE *stream, const __off64_t *pos)
{
  if (pos == NULL)
    return __set_errno (EINVAL);
  return fseeko64 (stream, *pos, SEEK_SET);
}

