/* proctest: sysconf's processor count (riscos-unixlib C2, C7).  The
   argument is the count expected: 1 normally, or what UnixLib$Processors
   says (1 to 64; anything else is ignored).  The Obey file Procs runs it
   with the variable unset, 3, 0 and "abc".  Must print PASS each time.  */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main (int argc, char **argv)
{
  long want = argc > 1 ? atol (argv[1]) : 1;
  /* Through a pointer: <unistd.h> declares sysconf "const", so a direct
     call could let the compiler assume errno unchanged.  */
  long (*volatile sc) (int) = sysconf;
  errno = 1234;
  long onln = sc (_SC_NPROCESSORS_ONLN);
  long conf = sc (_SC_NPROCESSORS_CONF);
  int ok = onln == want && conf == want && errno == 1234;
  printf ("processors online %ld, configured %ld (want %ld), errno kept: %s\n",
	  onln, conf, want, ok ? "PASS" : "FAIL");
  return !ok;
}
