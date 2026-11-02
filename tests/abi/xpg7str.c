/* <string.h>: strnlen, strndup, stpcpy and stpncpy are POSIX.1-2008, so
   they're declared without _GNU_SOURCE (2026; Gnumeric's plug-ins). Must
   compile in the default mode with implicit declarations as errors.  */
#include <stdlib.h>
#include <string.h>

int
main (void)
{
  char buf[8];
  char *d = strndup ("hello", 3);
  size_t n = strnlen ("hello", 4);

  stpncpy (stpcpy (buf, "ab"), "cd", 3);
  free (d);
  return (int) n - 4;
}
