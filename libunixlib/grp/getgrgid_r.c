/* getgrgid_r ()
 * Search for an entry with a matching group ID (re-entrant version).
 *
 * Copyright (c) 2000-2010 UnixLib Developers
 */

#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <grp.h>
#include <sys/types.h>

#include <pthread.h>
#include <internal/unix.h>

/* After the search loop: why __grpread stopped. A line too long for
   BUFFER leaves the stream short of its end (ERANGE, so the caller can
   try a bigger buffer); a read error is EIO; otherwise there's no such
   entry (0).  */
static int
search_end (FILE *stream)
{
  if (ferror (stream))
    return __set_errno (EIO), EIO;
  if (!feof (stream))
    return __set_errno (ERANGE), ERANGE;
  return 0;
}

/* 2026: POSIX: *result is NULL unless an entry is returned, and the
   return value is 0 (found, or no such entry) or an error number. These
   used to return -1 with *result left unset, so callers that check
   *result (GLib does) read an uninitialised pointer: on RISC OS there's
   no /etc/group, so every group lookup did. Found by riscos-gtk
   (Gnumeric's File > Properties).  */
int
getgrgid_r (gid_t gid, struct group *resbuf, char *buffer, size_t buflen,
	    struct group **result)
{
  PTHREAD_SAFE_CANCELLATION

  if (result != NULL)
    *result = NULL;

  if (resbuf == NULL || buffer == NULL)
    return __set_errno (EINVAL), EINVAL;

  FILE *stream = fopen ("/etc/group", "r");
  if (stream == NULL)
    return errno == ENOENT ? 0 : errno;	/* no group file: no entries */

  struct group *grp;
  while ((grp = __grpread (stream, resbuf, buffer, buflen)) != NULL)
    {
      if (resbuf->gr_gid == gid)
        break;
    }

  int err = grp == NULL ? search_end (stream) : 0;
  fclose (stream);

  if (grp == NULL)
    return err;

  if (result != NULL)
    *result = resbuf;

  return 0;
}
