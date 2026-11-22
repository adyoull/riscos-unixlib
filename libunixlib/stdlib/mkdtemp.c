/* mkdtemp ()
   Copyright (c) 2026 riscos-unixlib (unofficial fork).

   2026 (H3, from riscos-unixlib-web W5): POSIX.1-2008 mkdtemp was
   missing; WebKit's WTF (FileSystem.cpp) uses it.  Makes a new directory, mode 0700, from a
   template whose last six characters are "XXXXXX", the way mkstemp makes
   a file: mktemp picks a name, and mkdir's EEXIST means try again.  */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

char *
mkdtemp (char *file_template)
{
  size_t len = strlen (file_template);
  int tries;

  if (len < 6 || strcmp (file_template + len - 6, "XXXXXX") != 0)
    {
      errno = EINVAL;
      return NULL;
    }

  for (tries = 0; tries < 100; tries++)
    {
      memcpy (file_template + len - 6, "XXXXXX", 6);
      if (mktemp (file_template) == NULL || file_template[0] == '\0')
	{
	  if (errno == 0)
	    errno = EEXIST;
	  return NULL;
	}
      if (mkdir (file_template, 0700) == 0)
	return file_template;
      if (errno != EEXIST)
	return NULL;
    }

  errno = EEXIST;
  return NULL;
}
