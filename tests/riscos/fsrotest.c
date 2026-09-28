/* fsrotest: fsync()/fdatasync() on a read-only file must succeed, and the
   file must then close (before the fix fsync failed with EBADF). */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main (int argc, char **argv)
{
  const char *f = argc > 1 ? argv[1] : "/<Wimp$ScrapDir>/fsrotest";
  FILE *w = fopen (f, "w");
  if (!w) { perror (f); return 1; }
  fputs ("test\n", w); fclose (w);
  int fd = open (f, O_RDONLY), bad = 0;
  if (fd < 0) { perror ("open"); return 1; }
  int r = fsync (fd);
  printf ("fsync on a read-only file: %d%s%s\n", r, r ? " errno " : "", r ? strerror (errno) : "");
  bad |= r != 0;
  r = fdatasync (fd);
  printf ("fdatasync: %d\n", r);
  bad |= r != 0;
  r = close (fd);
  printf ("close: %d\n", r);
  bad |= r != 0;
  fd = open (f, O_WRONLY | O_APPEND);
  r = fd >= 0 ? fsync (fd) : -1;
  printf ("fsync on a writable file: %d\n", r);
  bad |= r != 0;
  close (fd);
  remove (f);
  printf ("%s\n", bad ? "FAIL" : "PASS");
  return bad;
}
