/* getpwnam_r ()
 * Search for an entry with a matching username (re-entrant version).
 * Written by Nick Burrett, 10 December 1997.
 * Copyright (c) 1997-2010 UnixLib Developers
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>
#include <errno.h>

#include <pthread.h>
#include <internal/unix.h>

/* After the search loop: why __pwdread stopped. A line too long for
   BUFFER leaves the stream short of its end and the buffer full (ERANGE,
   so the caller can try a bigger buffer); a read error is EIO; at the
   end of the file there's no such entry (0).  */
static int
search_end (FILE *stream, const char *buffer, size_t buflen)
{
  if (ferror (stream))
    return __set_errno (EIO), EIO;
  if (feof (stream))
    return 0;
  /* Stopped early. ERANGE only if the line filled the buffer (the reader
     asks fgets for buflen - 1 bytes); otherwise it's a line the reader
     can't take (a NUL in it), which a bigger buffer wouldn't help.  */
  if (buflen <= 2 || strlen (buffer) >= buflen - 2)
    return __set_errno (ERANGE), ERANGE;
  return __set_errno (EIO), EIO;
}

/* 2026: POSIX: *result is NULL unless an entry is returned, and the
   return value is 0 (found, or no such entry) or an error number. These
   used to return -1 with *result left unset, so callers that check
   *result (GLib does) read an uninitialised pointer when /etc/passwd
   had no such entry. Found by riscos-gtk
   (Gnumeric's File > Properties).  */
/* Search for an entry with a matching name.  */
int
getpwnam_r (const char *name, struct passwd *result_buf, char *buffer,
            size_t buflen, struct passwd **result)
{
  PTHREAD_SAFE_CANCELLATION

  if (result != NULL)
    *result = NULL;

  if (result_buf == NULL || buffer == NULL)
    return __set_errno (EINVAL), EINVAL;

  FILE *stream = fopen ("/etc/passwd", "r");

  struct passwd *p;
  int err = 0;
  if (stream == NULL)
    {
      /* No /etc/passwd (the usual case on RISC OS): the default entry,
	 whatever the name, as before.  */
      p = __pwddefault ();
      memcpy (result_buf, p, sizeof(struct passwd));
    }
  else
    {
      while ((p = __pwdread (stream, result_buf, buffer, buflen)) != NULL)
        {
          if (strcmp (result_buf->pw_name, name) == 0)
            break;
        }

      if (p == NULL)
	err = search_end (stream, buffer, buflen);
      fclose (stream);
   }

  if (p == NULL)
    return err;
  if (result != NULL)
    *result = result_buf;
  /* 2026: this used to copy pw_passwd to the start of BUFFER, where an
     entry read from /etc/passwd keeps pw_name, overwriting the name it
     had just found. Nothing needs the copy: the default entry's strings
     are static.  */

  return 0;
}
