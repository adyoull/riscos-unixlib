/* vhtest: the heap of a program started by vfork + exec must not grow over
   the copy of its parent (riscos-unixlib, 5.0.3.4-rc1; Rob Heaton's
   riscos-gcc16 report 08).

   SharedUnixLibrary keeps a copy of the parent above the child's memory
   limit while the child runs.  A child whose heap is in the Wimp slot
   (no dynamic area) used to grow its slot straight over that copy, and the
   parent then came back corrupted ("abort on data transfer", or wrong
   data).  Now the child's heap stops at its limit (malloc then uses mmap
   or fails), and the parent comes back intact.

   The parent fills 4 MB with a pattern, runs itself as a child through
   system () (vfork + exec), and the child (heap in the Wimp slot)
   allocates and fills 1 MB blocks up to 48 MB.  The parent then checks
   its pattern.  Must print PASS.  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int __dynamic_no_da = 1;	/* heap in the Wimp slot */

static int child (void)
{
  int n = 0;
  for (; n < 48; n++)
    {
      char *p = malloc (1 << 20);
      if (p == NULL)
	break;
      memset (p, 0xA5, 1 << 20);
    }
  printf ("child: allocated and filled %d MB\n", n);
  return 0;
}

int main (int argc, char **argv)
{
  static unsigned char pattern[4 << 20];
  char cmd[512];
  size_t i;
  int bad = 0;

  if (argc > 1 && strcmp (argv[1], "-child") == 0)
    return child ();

  for (i = 0; i < sizeof pattern; i++)
    pattern[i] = (unsigned char) (i * 7 + 3);
  snprintf (cmd, sizeof cmd, "%s -child", argv[0]);
  int rc = system (cmd);
  for (i = 0; i < sizeof pattern; i++)
    if (pattern[i] != (unsigned char) (i * 7 + 3))
      {
	bad = 1;
	break;
      }
  printf ("child exit status %d; parent's 4 MB %s\n", rc,
	  bad ? "CHANGED" : "intact");
  printf ("%s\n", bad || rc != 0 ? "FAIL" : "PASS");
  return bad;
}
