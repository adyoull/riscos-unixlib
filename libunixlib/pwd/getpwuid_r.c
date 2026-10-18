/* getpwuid_r ()
 * Search for an entry with a matching user ID (re-entrant version).
 * Written by Nick Burrett, 10 December 1997.
 * Copyright (c) 1997-2012 UnixLib Developers
 */

#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>

#include <pthread.h>
#include <internal/unix.h>
#include <string.h>

static int copy_default (struct passwd *to,
			 char *buffer,
			 size_t buflen,
			 struct passwd *from)
{
  char *buffp = buffer;
  size_t buffer_remaining = buflen;
  size_t member_len;

#define COPY_MEMBER(member) \
  if (from->member == NULL) \
    to->member = NULL; \
  else \
  { \
    member_len = strlen (from->member) + 1; \
    if (member_len > buffer_remaining) \
      return __set_errno (ERANGE); \
    memcpy (buffp, from->member, member_len); \
    to->member = buffp; \
    buffp += member_len; \
    buffer_remaining -= member_len; \
  }

  to->pw_uid = from->pw_uid;
  to->pw_gid = from->pw_gid;
  COPY_MEMBER (pw_name)
  COPY_MEMBER (pw_passwd)
  COPY_MEMBER (pw_gecos)
  COPY_MEMBER (pw_dir)
  COPY_MEMBER (pw_shell)

  return 0;
}

/* After the search loop: why __pwdread stopped. A line too long for
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
   *result (GLib does) read an uninitialised pointer when /etc/passwd
   had no such entry. Found by riscos-gtk
   (Gnumeric's File > Properties).  */
int
getpwuid_r (uid_t uid, struct passwd *resbuf, char *buffer, size_t buflen,
	    struct passwd **result)
{
  PTHREAD_SAFE_CANCELLATION

  if (result != NULL)
    *result = NULL;

  if (resbuf == NULL)
    return __set_errno (EINVAL), EINVAL;

  FILE *stream = fopen ("/etc/passwd", "r");
  if (stream == NULL)
    {
      /* No /etc/passwd (the usual case on RISC OS): the default entry,
	 copied into the caller's buffer.  */
      if (copy_default (resbuf, buffer, buflen, __pwddefault ()) != 0)
	return ERANGE;
      if (result != NULL)
	*result = resbuf;
      return 0;
    }

  struct passwd *p;
  while ((p = __pwdread (stream, resbuf, buffer, buflen)) != NULL)
    {
      if (resbuf->pw_uid == uid)
        break;
    }

  int err = p == NULL ? search_end (stream) : 0;
  fclose (stream);

  if (p == NULL)
    return err;

  if (result != NULL)
    *result = resbuf;

  return 0;
}
