/* setservent (), getservent (), endservent (), getservbyname (),
 * getservbyport ()
 * Copyright (c) 2000-2008 UnixLib Developers
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <internal/local.h>
#include <internal/unix.h>
#include <pthread.h>

/* File handle for the services file.  */
static FILE *servfile = NULL;

/* Set to 1 if the database file should be kept open between
   calls to these routines.  2026: no longer used: getservent () keeps
   its stream open until endservent (), and the lookups use their own.  */
static int keepopen = 0;

static int __setservent (int allowrewind);

/* 2026: changes whenever __getservent's static entry may change, so
   getservent_r (getserv_r.c) can tell whether the entry it couldn't copy
   (ERANGE) is still there to hand back.  */
unsigned int __servent_generation;
static struct servent *__getservent (FILE *file);

/* Open and rewind the services file.  */
void
setservent (int stayopen)
{
  PTHREAD_UNSAFE

  /* Record whether the file should be kept open */
  keepopen = stayopen;

  (void) __setservent (1);
}

/* Do the real work of opening/rewinding the services file.  */
static int
__setservent (int allowrewind)
{
  /* 2026: the static entry is about to mean something else.  */
  __servent_generation++;

  /* Open or rewind the file as necessary */
  if (servfile)
    {
      if (allowrewind)
	rewind (servfile);
    }
  else
    {
      servfile = fopen ("InetDBase:Services", "r");
    }

  return (servfile == NULL) ? -1 : 0;
}

/* Fetch the next entry from the services file.  */
struct servent *
getservent ()
{
  struct servent *serv;

  PTHREAD_UNSAFE

  /* Open the file if necessary */
  if (servfile == NULL)
    if (__setservent (0) == -1)
      return NULL;

  /* Do the actual read */
  serv = __getservent (servfile);

  /* 2026: the file stays open until endservent (), as in glibc: closing
     it here unless setservent (1) had been called meant every call
     reopened it and returned the first entry again, so a getservent ()
     loop never ended.  (getservbyname () and getservbyport () rewind,
     and still close it unless setservent (1) was called.)  */
  return serv;
}

/* Do the real work of getting an entry from the file.  */
static struct servent *
__getservent (FILE *file)
{
  static struct servent serv =
  {
    NULL, NULL, 0, NULL
  };

  char **item;
  char line[256];
  char *element;
  int aliases;

  /* Free up any memory in use */
  if (serv.s_name)
    {
      for (item = serv.s_aliases; *item; item++)
	free (*item);
      free (serv.s_name);
      free (serv.s_aliases);
      free (serv.s_proto);

      serv.s_name = NULL;
    }

  /* 2026: strtok_r, not strtok: strtok's single saved pointer belongs to
     the program, and getservbyname_r () and friends (getserv_r.c) are
     reentrant; a program's strtok loop that called one was left pointing
     into this function's dead stack frame.  And a line without a name,
     port and protocol (only blanks, say) is skipped: strdup (NULL) and
     atoi (NULL) used to crash on it.  */
  __servent_generation++;
  for (;;)
    {
      char *save, *name, *port, *protocol;

      /* Read a line from the file */
      if (__net_readline (file, line, sizeof (line) - 1) == NULL)
	return NULL;

      if ((name = strtok_r (line, " \t", &save)) == NULL
	  || (port = strtok_r (NULL, "/", &save)) == NULL
	  || (protocol = strtok_r (NULL, " \t", &save)) == NULL)
	continue;

      /* Out of memory: no entry (it used to go on with NULLs).  The
	 entry is only valid once s_name is set, which is what the next
	 call's freeing goes by.  */
      char *n = strdup (name), *pr = strdup (protocol);
      char **al = malloc (sizeof (char *));
      if (n == NULL || pr == NULL || al == NULL)
	{
	  free (n);
	  free (pr);
	  free (al);
	  errno = ENOMEM;
	  return NULL;
	}
      al[0] = NULL;
      aliases = 1;

      /* Extract the aliases */
      while ((element = strtok_r (NULL, " \t", &save)) != NULL)
	{
	  char **more = realloc (al, (aliases + 1) * sizeof (char *));
	  char *a = strdup (element);
	  if (more == NULL || a == NULL)
	    {
	      /* Out of memory: no entry (errno ENOMEM), not one with
		 aliases missing.  */
	      free (a);
	      if (more)
		al = more;
	      for (char **item = al; *item; item++)
		free (*item);
	      free (al);
	      free (n);
	      free (pr);
	      errno = ENOMEM;
	      return NULL;
	    }
	  al = more;
	  al[aliases - 1] = a;
	  al[aliases] = NULL;
	  aliases++;
	}
      serv.s_aliases = al;
      serv.s_proto = pr;
      serv.s_port = htons (atoi (port));
      serv.s_name = n;
      break;
    }

  return &serv;
}

/* Close the services file.  */
void
endservent (void)
{
  PTHREAD_UNSAFE

  /* 2026: an entry getservent_r couldn't copy (ERANGE) mustn't be
     handed back after this.  */
  __servent_generation++;

  /* If it's open, close it */
  if (servfile)
    {
      (void) fclose (servfile);
      servfile = 0;
    }
}

/* Search the services file for a given service name.  */
struct servent *
getservbyname (const char *name, const char *proto)
{
  struct servent *serv;
  char **alias;

  PTHREAD_UNSAFE

  /* 2026 (peer review): the lookups read their own stream, as in glibc:
     rewinding the one getservent () reads (and closing it) restarted or
     ended a getservent () walk that looked entries up as it went.  */
  FILE *file = fopen ("InetDBase:Services", "r");
  if (file == NULL)
    return NULL;

  /* Look through the file for a match */
  while ((serv = __getservent (file)) != NULL)
    {
      /* Give up now if the protocol doesn't match */
      if (proto && (strcmp (serv->s_proto, proto) != 0))
	continue;

      /* Does the offical name match? */
      if (strcmp (serv->s_name, name) == 0)
	break;

      /* Do any of the aliases match? */
      for (alias = serv->s_aliases; *alias; alias++)
	{
	  if (strcmp (*alias, name) == 0)
	    break;
	}

      /* Did any of the aliases match? */
      if (*alias)
	break;
    }

  fclose (file);
  return serv;
}

/* Search the services file for a given port.  */
struct servent *
getservbyport (int port, const char *proto)
{
  struct servent *serv;

  PTHREAD_UNSAFE

  /* 2026 (peer review): the lookups read their own stream, as in glibc:
     rewinding the one getservent () reads (and closing it) restarted or
     ended a getservent () walk that looked entries up as it went.  */
  FILE *file = fopen ("InetDBase:Services", "r");
  if (file == NULL)
    return NULL;

  /* Look through the file for a match */
  while ((serv = __getservent (file)) != NULL)
    {
      /* Give up now if the protocol doesn't match */
      if (proto && (strcmp (serv->s_proto, proto) != 0))
	continue;

      /* If the port matches, we've found it */
      if (serv->s_port == port)
	break;

    }

  fclose (file);
  return serv;
}
