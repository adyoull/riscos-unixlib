/* Host test for stdlib/mkdtemp.c (H3, from riscos-unixlib-web W5), using the
   host's mktemp and mkdir.  */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks, failed;
static void check (int ok, const char *what)
{
  checks++;
  if (!ok)
    {
      failed++;
      printf ("FAIL %s\n", what);
    }
}

int main (void)
{
  char base[] = "out/mkdtemp-test";
  mkdir ("out", 0755);
  char t1[64], t2[64];
  snprintf (t1, sizeof t1, "%s-XXXXXX", base);
  snprintf (t2, sizeof t2, "%s-XXXXXX", base);
  char *a = mkdtemp (t1), *b = mkdtemp (t2);
  struct stat st;
  check (a == t1, "returns the template");
  check (a && strncmp (a, base, strlen (base)) == 0 && strstr (a, "XXXXXX") == NULL,
	 "X's replaced, prefix kept");
  check (a && stat (a, &st) == 0 && S_ISDIR (st.st_mode), "a directory was made");
  check (a && (st.st_mode & 0777) == 0700, "mode 0700");
  check (a && b && strcmp (a, b) != 0, "two calls give two names");
  char bad[] = "out/nox";
  errno = 0;
  check (mkdtemp (bad) == NULL && errno == EINVAL, "no XXXXXX: EINVAL");
  char bad2[] = "out/x-XXXXX";
  errno = 0;
  check (mkdtemp (bad2) == NULL && errno == EINVAL, "five X's: EINVAL");
  char nodir[] = "out/no/such/dir-XXXXXX";
  errno = 0;
  check (mkdtemp (nodir) == NULL && errno == ENOENT, "missing parent: ENOENT");
  if (a) rmdir (a);
  if (b) rmdir (b);
  printf ("mkdtemp: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
