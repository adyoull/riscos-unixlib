/* miditest: play a scale and chords through UnixLib's /dev/midi
   (the MIDISynth module, or external MIDI through the MIDI module). */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

static int fd;
static void out (const unsigned char *m, int n)
{
  if (write (fd, m, n) != n) perror ("write /dev/midi");
}
static void wait_ms (int ms)
{
  struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
  nanosleep (&ts, NULL);
}

int main (void)
{
  fd = open ("/dev/midi", O_WRONLY);
  if (fd < 0)
    {
      perror ("/dev/midi");
      if (errno == ENODEV) printf ("Neither the MIDISynth module nor the MIDI module is loaded.\n");
      return 1;
    }
  unsigned char pc[] = { 0xC0, 0 };			/* piano */
  out (pc, 2);
  static const int scale[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
  for (int i = 0; i < 8; i++)
    {
      unsigned char on[] = { 0x90, scale[i], 100 };
      out (on, 3);
      wait_ms (250);
      unsigned char off[] = { 0x90, scale[i], 0 };	/* note on, velocity 0 */
      out (off, 3);
    }
  unsigned char strings[] = { 0xC1, 48 };
  out (strings, 2);
  unsigned char chord[] = { 0x91, 48, 90, 55, 90, 64, 90, 0x99, 49, 110 };	/* running status + cymbal */
  out (chord, sizeof chord);
  wait_ms (1500);
  printf ("SEQ_RESET (all notes off)\n");
  ioctl (fd, SNDCTL_SEQ_RESET, 0);
  wait_ms (300);
  close (fd);
  printf ("done\n");
  return 0;
}
