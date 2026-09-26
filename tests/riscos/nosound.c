/* nosound: does nothing and quits.  Before the fix, any UnixLib program
   quitting stopped DigitalRenderer sound from other programs. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main (int argc, char **argv)
{
  if (argc > 1)
    sleep (atoi (argv[1]));
  printf ("nosound: quitting without playing anything\n");
  return 0;
}
