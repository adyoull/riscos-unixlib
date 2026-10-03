/* getservbyname_r (), getservbyport_r (), getservent_r ()
   Copyright (c) 2026 UnixLib Developers.

   2026: <netdb.h> declared these but the library didn't have them, so
   configure checks that only compile (GLib's meson) took them as present
   and the link failed.  They call the non-reentrant functions with thread
   switching held off and copy the result into the caller's buffer.

   As glibc: 0 on success with *result set; 0 with *result NULL if there is
   no such service; ERANGE (with *result NULL) if BUF is too small.  */

#include <errno.h>
#include <netdb.h>
#include <stddef.h>
#include <string.h>

#include <pthread.h>
#include <internal/unix.h>

static int
copy_servent (const struct servent *src, struct servent *dst,
	      char *buf, size_t buflen, struct servent **result)
{
  size_t naliases = 0, need, i, len;
  char **aliases;
  char *p;

  *result = NULL;
  if (src == NULL)
    return 0;

  while (src->s_aliases && src->s_aliases[naliases])
    naliases++;

  /* Pointer array first (aligned), then the strings.  */
  need = (naliases + 1) * sizeof (char *);
  need += strlen (src->s_name) + 1;
  need += (src->s_proto ? strlen (src->s_proto) : 0) + 1;
  for (i = 0; i < naliases; i++)
    need += strlen (src->s_aliases[i]) + 1;

  i = (sizeof (char *) - ((size_t) buf & (sizeof (char *) - 1)))
      & (sizeof (char *) - 1);
  if (buflen < i || buflen - i < need)
    return ERANGE;

  aliases = (char **) (buf + i);
  p = (char *) (aliases + naliases + 1);

  len = strlen (src->s_name) + 1;
  dst->s_name = memcpy (p, src->s_name, len);
  p += len;

  len = (src->s_proto ? strlen (src->s_proto) : 0) + 1;
  dst->s_proto = memcpy (p, src->s_proto ? src->s_proto : "", len);
  p += len;

  for (i = 0; i < naliases; i++)
    {
      len = strlen (src->s_aliases[i]) + 1;
      aliases[i] = memcpy (p, src->s_aliases[i], len);
      p += len;
    }
  aliases[naliases] = NULL;
  dst->s_aliases = aliases;
  dst->s_port = src->s_port;

  *result = dst;
  return 0;
}

int
getservbyname_r (const char *name, const char *proto,
		 struct servent *result_buf, char *buf, size_t buflen,
		 struct servent **result)
{
  PTHREAD_UNSAFE

  return copy_servent (getservbyname (name, proto), result_buf, buf, buflen,
		       result);
}

int
getservbyport_r (int port, const char *proto,
		 struct servent *result_buf, char *buf, size_t buflen,
		 struct servent **result)
{
  PTHREAD_UNSAFE

  return copy_servent (getservbyport (port, proto), result_buf, buf, buflen,
		       result);
}

#ifndef __TARGET_SCL__
int
getservent_r (struct servent *result_buf, char *buf, size_t buflen,
	      struct servent **result)
{
  PTHREAD_UNSAFE

  return copy_servent (getservent (), result_buf, buf, buflen, result);
}
#endif
