/* Host test shim, included first: UnixLib's __set_errno, and fopen of
   /etc/group and /etc/passwd redirected to the test's own files.  */
#include <errno.h>
#include <stdio.h>
#define __set_errno(val) (errno = (val), -1)
FILE *test_fopen (const char *, const char *);
#define fopen(p, m) test_fopen (p, m)
