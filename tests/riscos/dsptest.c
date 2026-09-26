/* dsptest: play a tone (or a PCM .wav) through UnixLib's /dev/dsp.
   dsptest [-s secs] [-r rate] [-m] [-f s16|s16be|u8|s8|ulaw] [-n] [-F frag]
           [-d] [-q] [file.wav]
   -m mono, -n O_NONBLOCK, -F log2 fragment size (SETFRAGMENT 8 x 2^n),
   -d force DigitalRenderer (sets UnixLib$DSP), -q print nothing while playing.
   Prints which output is in use (GETCAPS: SharedSoundBuffer reports
   DSP_CAP_REALTIME), the fragment sizes and the latency (GETODELAY). */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

static int fmt_of (const char *s)
{
  if (!strcmp (s, "s16")) return AFMT_S16_LE;
  if (!strcmp (s, "s16be")) return AFMT_S16_BE;
  if (!strcmp (s, "u8")) return AFMT_U8;
  if (!strcmp (s, "s8")) return AFMT_S8;
  if (!strcmp (s, "ulaw")) return AFMT_MU_LAW;
  fprintf (stderr, "unknown format %s\n", s); exit (1);
}

static unsigned char lin2ulaw (int s)
{
  int sign = (s >> 8) & 0x80, exp, mant;
  if (sign) s = -s;
  if (s > 32635) s = 32635;
  s += 0x84;
  for (exp = 7; exp > 0 && !(s & (0x4000 >> (7 - exp))); exp--) ;
  mant = (s >> (exp + 3)) & 0x0f;
  return (unsigned char) ~(sign | (exp << 4) | mant);
}

int main (int argc, char **argv)
{
  double secs = 3;
  int rate = 44100, ch = 2, fmt = AFMT_S16_LE, nonblock = 0, frag = 0, quiet = 0, i;
  const char *wav = NULL;
  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-s") && i + 1 < argc) secs = atof (argv[++i]);
      else if (!strcmp (argv[i], "-r") && i + 1 < argc) rate = atoi (argv[++i]);
      else if (!strcmp (argv[i], "-f") && i + 1 < argc) fmt = fmt_of (argv[++i]);
      else if (!strcmp (argv[i], "-F") && i + 1 < argc) frag = atoi (argv[++i]);
      else if (!strcmp (argv[i], "-m")) ch = 1;
      else if (!strcmp (argv[i], "-n")) nonblock = 1;
      else if (!strcmp (argv[i], "-q")) quiet = 1;
      else if (!strcmp (argv[i], "-d")) setenv ("UnixLib$DSP", "DigitalRenderer", 1);
      else wav = argv[i];
    }

  FILE *in = NULL;
  if (wav)
    {
      unsigned char h[44];
      if (!(in = fopen (wav, "rb")) || fread (h, 1, 44, in) != 44 || memcmp (h, "RIFF", 4) || memcmp (h + 8, "WAVE", 4))
        { fprintf (stderr, "%s: not a WAV file\n", wav); return 1; }
      ch = h[22]; rate = h[24] | h[25] << 8 | h[26] << 16 | h[27] << 24;
      fmt = h[34] == 8 ? AFMT_U8 : AFMT_S16_LE;
      secs = 1e9;
    }

  int fd = open ("/dev/dsp", O_WRONLY | (nonblock ? O_NONBLOCK : 0));
  if (fd < 0) { perror ("/dev/dsp"); return 1; }
  if (frag) { int f = (8 << 16) | frag; ioctl (fd, SNDCTL_DSP_SETFRAGMENT, &f); }
  int want = fmt;
  ioctl (fd, SNDCTL_DSP_SETFMT, &fmt);
  ioctl (fd, SNDCTL_DSP_CHANNELS, &ch);
  ioctl (fd, SNDCTL_DSP_SPEED, &rate);
  int caps = 0, blk = 0;
  ioctl (fd, SNDCTL_DSP_GETCAPS, &caps);
  ioctl (fd, SNDCTL_DSP_GETBLKSIZE, &blk);
  printf ("output: %s\n", (caps & DSP_CAP_REALTIME) ? "SharedSoundBuffer (mixes with other programs)" : "DigitalRenderer");
  printf ("format %#x%s, %d channel(s), %d Hz, block %d bytes\n", fmt, fmt != want ? " (asked for another)" : "", ch, rate, blk);

  int bps = (fmt == AFMT_S16_LE || fmt == AFMT_S16_BE) ? 2 : 1, fs = bps * ch;
  static unsigned char buf[16384];
  long frames_total = (long) (secs * rate), t = 0, eagain = 0;
  clock_t start = clock ();
  int max_delay = 0;
  while (t < frames_total)
    {
      int n = sizeof buf / fs, k, c;
      if (in)
        {
          n = fread (buf, fs, n, in);
          if (n <= 0) break;
        }
      else
        {
          if (n > frames_total - t) n = frames_total - t;
          for (k = 0; k < n; k++)
            for (c = 0; c < ch; c++)
              {
                double ph = (double) (t + k) / rate;
                /* A major chord, left and right a little different */
                int v = (int) (7000 * (sin (2 * M_PI * 440 * ph) + sin (2 * M_PI * (c ? 554.37 : 659.25) * ph)));
                unsigned char *p = buf + k * fs + c * bps;
                switch (fmt)
                  {
                  case AFMT_S16_LE: p[0] = v; p[1] = v >> 8; break;
                  case AFMT_S16_BE: p[1] = v; p[0] = v >> 8; break;
                  case AFMT_U8: p[0] = (unsigned char) ((v >> 8) + 128); break;
                  case AFMT_S8: p[0] = (unsigned char) (v >> 8); break;
                  default: p[0] = lin2ulaw (v); break;
                  }
              }
        }
      int off = 0, len = n * fs;
      while (off < len)
        {
          int w = write (fd, buf + off, len - off);
          if (w < 0 && errno == EAGAIN) { eagain++; continue; }
          if (w < 0) { perror ("write"); return 1; }
          off += w;
        }
      t += n;
      int d = 0;
      if (ioctl (fd, SNDCTL_DSP_GETODELAY, &d) == 0 && d > max_delay) max_delay = d;
      if (!quiet && (t / rate) != ((t - n) / rate))
        printf ("  %lds played, queued %d ms\n", t / rate, d * 1000 / (fs * rate));
    }
  ioctl (fd, SNDCTL_DSP_SYNC, 0);
  close (fd);
  printf ("done: %.2f s of sound in %.2f s, max queued %d ms%s\n", (double) t / rate,
          (double) (clock () - start) / CLOCKS_PER_SEC, max_delay * 1000 / (fs * rate),
          nonblock ? "" : "");
  if (nonblock) printf ("EAGAIN returned %ld times (non-blocking)\n", eagain);
  return 0;
}
