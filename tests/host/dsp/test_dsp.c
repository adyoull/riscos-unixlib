/* Host tests for UnixLib's /dev/dsp (sound/dsp.c) against a fake
   SharedSoundBuffer/StreamManager and a fake DigitalRenderer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/soundcard.h>
#include "internal/dev.h"
#include "riscos.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf ("FAIL %s:%d: ", __FILE__, __LINE__); printf (__VA_ARGS__); printf ("\n"); } } while (0)

static struct __unixlib_fd_handle H;
static struct __unixlib_fd FD;

static int dsp_open (int flags)
{
  FD.devicehandle = &H; FD.fflag = flags;
  H.handle = __dspopen (&FD, "/dev/dsp", flags);
  return H.handle == (void *) -1 ? -1 : 0;
}
static int ioc (unsigned long req, int v) { int x = v; return __dspioctl (&FD, req, &x) < 0 ? -9999 : x; }

static short *out16 (void) { return (short *) F.out; }

static void test_basic_s16_stereo (void)
{
  fake_reset ();
  CHECK (dsp_open (O_WRONLY) == 0, "open");
  CHECK (F.opens == 0, "stream must not open until the first write");
  CHECK (ioc (SNDCTL_DSP_SPEED, 22050) == 22050, "speed");
  short buf[4000];
  for (int i = 0; i < 4000; i++) buf[i] = (short) (i * 7);
  CHECK (__dspwrite (&FD, buf, sizeof buf) == (int) sizeof buf, "write");
  CHECK (F.opens == 1 && F.rate == 22050, "opened at 22050 (rate %d)", F.rate);
  CHECK (strcmp (F.name, "testprog") == 0, "stream named after the program");
  CHECK (F.volume == 0xFFFFFFFFu, "full volume");
  CHECK (F.out_len == (int) sizeof buf && memcmp (F.out, buf, sizeof buf) == 0, "samples passed through unchanged");
  CHECK (!F.paused, "started once a fragment was queued");
  CHECK (__dspclose (&FD) == 0, "close");
  CHECK (F.played == F.added, "close drains (%d of %d played)", F.played, F.added);
  CHECK (F.closes == 1 && !F.open, "stream closed");
}

static void test_formats (void)
{
  struct { int fmt, ch; unsigned char in[4]; int inlen; short l, r; } t[] = {
    { AFMT_U8, 1, {0x80}, 1, 0, 0 },
    { AFMT_U8, 1, {0xFF}, 1, 127 << 8, 127 << 8 },
    { AFMT_U8, 2, {0x00, 0xFF}, 2, -32768, 127 << 8 },
    { AFMT_S8, 2, {0x80, 0x7F}, 2, -32768, 127 << 8 },
    { AFMT_S16_BE, 2, {0x12, 0x34, 0xFE, 0xDC}, 4, 0x1234, (short) 0xFEDC },
    { AFMT_S16_LE, 1, {0x34, 0x12}, 2, 0x1234, 0x1234 },
    { AFMT_MU_LAW, 1, {0xFF}, 1, 0, 0 },
    { AFMT_MU_LAW, 1, {0x00}, 1, -32124, -32124 },
    { AFMT_MU_LAW, 1, {0x80}, 1, 32124, 32124 },
  };
  for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
    {
      fake_reset ();
      dsp_open (O_WRONLY);
      CHECK (ioc (SNDCTL_DSP_SETFMT, t[i].fmt) == t[i].fmt, "fmt %x accepted", t[i].fmt);
      CHECK (ioc (SNDCTL_DSP_CHANNELS, t[i].ch) == t[i].ch, "channels");
      unsigned char big[4096];
      for (int k = 0; k < 4096; k += t[i].inlen) memcpy (big + k, t[i].in, t[i].inlen);
      int n = (4096 / t[i].inlen) * t[i].inlen;
      CHECK (__dspwrite (&FD, big, n) == n, "write fmt %x", t[i].fmt);
      CHECK (F.out_len == n / t[i].inlen * 4, "out len %d", F.out_len);
      CHECK (out16 ()[0] == t[i].l && out16 ()[1] == t[i].r, "case %u: got %d,%d want %d,%d", i, out16 ()[0], out16 ()[1], t[i].l, t[i].r);
      __dspclose (&FD);
    }
  fake_reset ();
  dsp_open (O_WRONLY);
  CHECK (ioc (SNDCTL_DSP_SETFMT, AFMT_U16_LE) == AFMT_S16_LE, "unsupported format -> S16_LE");
  CHECK (ioc (SNDCTL_DSP_SETFMT, AFMT_QUERY) == AFMT_S16_LE, "query");
  CHECK (ioc (SNDCTL_DSP_CHANNELS, 6) == 2, "6 channels -> 2");
  CHECK (ioc (SNDCTL_DSP_STEREO, 0) == 0 && ioc (SNDCTL_DSP_CHANNELS, 0) == 1, "STEREO 0 -> mono");
  CHECK (ioc (SNDCTL_DSP_GETFMTS, 0) == (AFMT_S16_LE|AFMT_S16_BE|AFMT_U8|AFMT_S8|AFMT_MU_LAW), "GETFMTS");
  CHECK (ioc (SNDCTL_DSP_SPEED, 200000) == 96000 && ioc (SNDCTL_DSP_SPEED, 100) == 4000, "rate clamps");
  __dspclose (&FD);
}

static void test_partial_frames (void)
{
  fake_reset ();
  dsp_open (O_WRONLY);
  short src[1000];
  for (int i = 0; i < 1000; i++) src[i] = (short) (i * 31 - 9000);
  const unsigned char *p = (const unsigned char *) src;
  int total = sizeof src, off = 0, sizes[] = {1, 3, 2, 7, 5, 1, 1, 1, 13, 999};
  for (int k = 0; off < total; k++)
    {
      int n = sizes[k % 10]; if (off + n > total) n = total - off;
      CHECK (__dspwrite (&FD, p + off, n) == n, "odd write %d", n);
      off += n;
    }
  CHECK (F.out_len == total && memcmp (F.out, src, total) == 0, "odd-sized writes reassemble exactly (%d)", F.out_len);
  __dspclose (&FD);
}

static void test_blocking_and_latency (void)
{
  fake_reset ();
  dsp_open (O_WRONLY);
  static short buf[44100 * 2];	/* 1 s */
  CHECK (__dspwrite (&FD, buf, sizeof buf) == (int) sizeof buf, "1 s blocking write");
  int cap = 4096 * 8;
  CHECK (F.max_queued <= cap, "never queued more than the capacity (%d > %d)", F.max_queued, cap);
  CHECK (F.now_us >= 700000, "blocked while it played (%lld us)", F.now_us);
  CHECK (F.blocksize == 4096, "usual block size 4096 (%d)", F.blocksize);
  audio_buf_info bi;
  CHECK (__dspioctl (&FD, SNDCTL_DSP_GETOSPACE, &bi) == 0 && bi.fragsize == 4096 && bi.fragstotal == 8, "GETOSPACE %d %d", bi.fragsize, bi.fragstotal);
  int d = ioc (SNDCTL_DSP_GETODELAY, 0);
  CHECK (d > 0 && d <= cap, "GETODELAY %d", d);
  CHECK (ioc (SNDCTL_DSP_SYNC, 0) == 0 && F.played == F.added, "SYNC drains");
  CHECK (F.underruns_us == 0, "no underruns while fed (%lld us)", F.underruns_us);
  count_info ci;
  CHECK (__dspioctl (&FD, SNDCTL_DSP_GETOPTR, &ci) == 0 && ci.bytes == (int) sizeof buf, "GETOPTR bytes %d", ci.bytes);
  CHECK (F.ints_toggles > 0, "waits re-enable thread switches");
  __dspclose (&FD);
}

static void test_fragment_setting (void)
{
  fake_reset ();
  dsp_open (O_WRONLY);
  ioc (SNDCTL_DSP_CHANNELS, 1);			/* mono S16: 2 bytes/frame */
  int fr = (4 << 16) | 10;
  CHECK (__dspioctl (&FD, SNDCTL_DSP_SETFRAGMENT, &fr) == 0, "SETFRAGMENT 4 x 1024");
  CHECK (ioc (SNDCTL_DSP_GETBLKSIZE, 0) == 1024, "blksize in program bytes (%d)", ioc (SNDCTL_DSP_GETBLKSIZE, 0));
  audio_buf_info bi;
  __dspioctl (&FD, SNDCTL_DSP_GETOSPACE, &bi);
  CHECK (bi.fragstotal == 4 && bi.bytes == 4096, "4 frags, 4096 program bytes free (%d %d)", bi.fragstotal, bi.bytes);
  static short buf[20000];
  __dspwrite (&FD, buf, sizeof buf);
  CHECK (F.max_queued <= 8192, "queue limited to 4 x 2048 output bytes (%d)", F.max_queued);
  __dspclose (&FD);
}

static void test_nonblock (void)
{
  fake_reset ();
  dsp_open (O_WRONLY | O_NONBLOCK);
  static short buf[44100 * 2];
  int n = __dspwrite (&FD, buf, sizeof buf);
  CHECK (n > 0 && n <= 4096 * 8, "non-blocking write takes what fits (%d)", n);
  CHECK (F.yields == 0, "non-blocking write never waits");
  CHECK (!F.paused, "full queue starts playback");
  errno = 0;
  CHECK (__dspwrite (&FD, buf, sizeof buf) == -1 && errno == EAGAIN, "full -> EAGAIN");
  fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield (); fake_yield ();
  CHECK (__dspwrite (&FD, buf, sizeof buf) > 0, "room again after playing");
  int before = F.closes;
  __dspclose (&FD);
  CHECK (F.closes == before + 1 && F.played < F.added, "non-blocking close doesn't wait");
}

static void test_short_sound_starts (void)
{
  fake_reset ();
  dsp_open (O_WRONLY);
  short beep[200] = {0};
  __dspwrite (&FD, beep, sizeof beep);
  CHECK (F.paused, "tiny write waits for more before starting");
  CHECK (ioc (SNDCTL_DSP_POST, 0) == 0 && !F.paused, "POST starts it");
  __dspclose (&FD);
  CHECK (F.played == F.added, "and close plays it out");
}

static void test_reset (void)
{
  fake_reset ();
  dsp_open (O_WRONLY);
  static short buf[8000];
  __dspwrite (&FD, buf, sizeof buf);
  CHECK (ioc (SNDCTL_DSP_RESET, 0) == 0 && !F.open, "RESET drops the queue");
  CHECK (__dspwrite (&FD, buf, sizeof buf) == (int) sizeof buf && F.open && F.opens == 2, "writing again reopens");
  __dspclose (&FD);
}

static void test_stuck_stream (void)
{
  fake_reset ();
  dsp_open (O_WRONLY);
  F.stall = 1;
  static short buf[44100 * 2];
  int n = __dspwrite (&FD, buf, sizeof buf);
  CHECK (n > 0 && n < (int) sizeof buf, "stalled output: write returns short instead of hanging (%d)", n);
  CHECK (F.now_us < 5000000, "gave up after ~2 s (%lld us)", F.now_us);
  __dspclose (&FD);
  CHECK (F.now_us < 8000000, "close didn't hang either");
}

static void test_exit (void)
{
  fake_reset ();
  dsp_open (O_WRONLY);
  static short buf[8000];
  __dspwrite (&FD, buf, sizeof buf);
  __dsp_exit ();
  CHECK (!F.open && F.closes == 1, "exit closes an open stream");
  fake_reset ();
  __dsp_exit ();
  CHECK (F.closes == 0, "exit without sound touches nothing");
}

/* DigitalRenderer path, including the exit bug. */
static void test_dr (void)
{
  fake_reset ();
  F.modules = 0;
  /* Another program is playing through DigitalRenderer.  */
  dr_state = 1; dr_nbuf = 77; dr_activations = dr_deactivations = dr_numbuf_calls = 0;
  __dsp_exit ();
  CHECK (dr_state == 1 && dr_deactivations == 0 && dr_numbuf_calls == 0, "BUG FIX: exit of a program that never played leaves DigitalRenderer alone");

  CHECK (dsp_open (O_WRONLY) == 0, "DR open");
  ioc (SNDCTL_DSP_SPEED, 22050);
  ioc (SNDCTL_DSP_CHANNELS, 2);
  CHECK (dr_state == 1 && dr_deactivations == 0 && dr_nbuf == 77, "open + ioctls don't stop or reconfigure the other program");
  CHECK (__dspclose (&FD) == 0 && dr_state == 1 && dr_deactivations == 0, "close without writing leaves it alone");

  dsp_open (O_WRONLY);
  short s[1024] = {0};
  CHECK (__dspwrite (&FD, s, sizeof s) == (int) sizeof s, "DR write");
  CHECK (dr_deactivations == 1 && dr_activations == 1 && dr_state == 1, "writing takes DigitalRenderer over (one user at a time)");
  CHECK (dr_streamed == 1024, "samples streamed (%d)", dr_streamed);
  __dsp_exit ();
  CHECK (dr_state == 0, "exit of the program that played deactivates it");

  fake_reset (); F.modules = 1; F.env_dsp = "DigitalRenderer"; dr_state = 0; dr_activations = 0;
  dsp_open (O_WRONLY);
  __dspwrite (&FD, s, sizeof s);
  CHECK (F.opens == 0 && dr_activations == 1, "UnixLib$DSP=DigitalRenderer forces the old path");
  __dspclose (&FD);
  fake_reset (); F.modules = 0; F.env_dsp = "SharedSound";
  errno = 0;
  CHECK (dsp_open (O_WRONLY) == -1 && errno == ENODEV, "UnixLib$DSP=SharedSound without the modules -> ENODEV");
}

/* 2026: a second open shares the first one's stream and settings; closing
   it leaves the first one playing (SDL probes the device like this).  */
static void test_two_opens (void)
{
  static struct __unixlib_fd_handle H2; static struct __unixlib_fd FD2;
  static short buf[8192];
  fake_reset ();
  CHECK (dsp_open (O_WRONLY) == 0, "first open");
  CHECK (ioc (SNDCTL_DSP_SPEED, 22050) == 22050, "first: 22050 Hz");
  CHECK (__dspwrite (&FD, buf, sizeof buf) == (int) sizeof buf, "first: write");
  FD2.devicehandle = &H2; FD2.fflag = O_WRONLY;
  H2.handle = __dspopen (&FD2, "/dev/dsp", O_WRONLY);
  CHECK (H2.handle != (void *) -1, "second open");
  CHECK (F.open && F.closes == 0, "second open doesn't close the first one's stream");
  int x = 0;
  CHECK (__dspioctl (&FD, SOUND_PCM_READ_RATE, &x) == 0 && x == 22050, "second open doesn't reset the rate (%d)", x);
  CHECK (__dspclose (&FD2) == 0 && F.open && F.closes == 0, "closing the second leaves the stream open");
  int blocks = F.blocks, acts = dr_activations;
  CHECK (__dspwrite (&FD, buf, sizeof buf) == (int) sizeof buf, "first: write after the second closed");
  CHECK (F.blocks > blocks && dr_activations == acts, "still SharedSoundBuffer, not DigitalRenderer");
  __dspclose (&FD);
  CHECK (!F.open && F.closes == 1, "last close closes the stream");
}

/* 2026: SOUND_PCM_READ_* have the same low 16 bits as SPEED, SETFMT and
   CHANNELS; READ_RATE with 0 used to set 4000 Hz.  */
static void test_read_ioctls (void)
{
  static short buf[4096];
  int x;
  fake_reset ();
  dsp_open (O_WRONLY);
  ioc (SNDCTL_DSP_SPEED, 22050);
  ioc (SNDCTL_DSP_CHANNELS, 1);
  __dspwrite (&FD, buf, sizeof buf);
  x = 0;
  CHECK (__dspioctl (&FD, SOUND_PCM_READ_RATE, &x) == 0 && x == 22050, "READ_RATE gives 22050 (%d)", x);
  CHECK (F.rate == 22050, "READ_RATE doesn't change the stream's rate (%d)", F.rate);
  x = 0;
  CHECK (__dspioctl (&FD, SOUND_PCM_READ_CHANNELS, &x) == 0 && x == 1, "READ_CHANNELS gives 1 (%d)", x);
  x = 0;
  CHECK (__dspioctl (&FD, SOUND_PCM_READ_BITS, &x) == 0 && x == 16, "READ_BITS gives 16 (%d)", x);
  CHECK (ioc (SNDCTL_DSP_SETFMT, AFMT_QUERY) == AFMT_S16_LE, "READ_BITS didn't change the format");
  /* The same requests as encoded when <sys/ioctl.h> came first (_IOR,
     direction 0x40000000).  */
  x = 0;
  CHECK (__dspioctl (&FD, 0x40045002, &x) == 0 && x == 22050 && F.rate == 22050, "READ_RATE (_IOR encoding) gives 22050 (%d), rate %d", x, F.rate);
  x = 0;
  CHECK (__dspioctl (&FD, 0x40045006, &x) == 0 && x == 1, "READ_CHANNELS (_IOR encoding) gives 1 (%d)", x);
  /* GETODELAY with _IOR (0x40045017) is SNDCTL_DSP_PROFILE in the other
     encoding: it must still be answered as GETODELAY.  */
  x = -5;
  CHECK (__dspioctl (&FD, 0x40045017, &x) == 0 && x >= 0, "GETODELAY (_IOR encoding) answered (%d)", x);
  x = 0;
  CHECK (__dspioctl (&FD, 0x40045010, &x) == 0 && x == PCM_ENABLE_OUTPUT, "GETTRIGGER (_IOR encoding) answered (%d)", x);
  __dspclose (&FD);
  /* DigitalRenderer path too.  */
  fake_reset (); F.modules = 0; dr_state = 0;
  dsp_open (O_WRONLY);
  ioc (SNDCTL_DSP_SPEED, 22050);
  x = 0;
  CHECK (__dspioctl (&FD, SOUND_PCM_READ_RATE, &x) == 0 && x == 22050, "DR: READ_RATE gives 22050 (%d)", x);
  __dspclose (&FD);
}

/* 2026: a fork()/vfork() child's _exit() must not close the parent's
   stream or stop its DigitalRenderer session.  */
static void test_fork_child_exit (void)
{
  static short buf[4096];
  fake_reset (); fake_pid = 100;
  dsp_open (O_WRONLY);
  __dspwrite (&FD, buf, sizeof buf);
  fake_pid = 200;			/* the child */
  __dsp_exit ();
  CHECK (F.open && F.closes == 0, "a child's exit leaves the parent's stream open");
  fake_pid = 100;
  __dsp_exit ();
  CHECK (!F.open && F.closes == 1, "the parent's own exit closes it");

  /* A vfork child writes first on the parent's descriptor (so the stream
     is the child's) and exits: the descriptor is still the parent's, so
     its next write must still go to SharedSoundBuffer.  */
  fake_reset (); fake_pid = 100;
  dsp_open (O_WRONLY);
  fake_pid = 200;
  __dspwrite (&FD, buf, sizeof buf);
  __dsp_exit ();
  fake_pid = 100;
  int acts = dr_activations, opens = F.opens;
  CHECK (__dspwrite (&FD, buf, sizeof buf) == (int) sizeof buf && F.opens == opens + 1 && dr_activations == acts,
	 "after a vfork child played and exited, the parent's descriptor still uses SharedSoundBuffer");
  __dsp_exit ();

  fake_reset (); F.modules = 0; dr_state = 0; dr_deactivations = 0; fake_pid = 100;
  dsp_open (O_WRONLY);
  __dspwrite (&FD, buf, sizeof buf);
  fake_pid = 200;
  __dsp_exit ();
  CHECK (dr_state == 1 && dr_deactivations == 0, "DR: a child's exit leaves the parent's session alone");
  fake_pid = 100;
  __dsp_exit ();
  CHECK (dr_state == 0, "DR: the parent's own exit stops it");
}

/* 2026: program B takes DigitalRenderer over from A: A's exit must not stop
   B's sound.  UnixLib$DSPOwner says who has it.  */
static void test_dr_takeover (void)
{
  static short buf[2048];
  fake_reset (); F.modules = 0; dr_state = 0; dr_deactivations = dr_activations = 0; fake_pid = 100;
  dsp_open (O_WRONLY);
  __dspwrite (&FD, buf, sizeof buf);
  CHECK (F.var_set && strcmp (F.var_owner, "100") == 0, "activating records the owner (%s)", F.var_owner);
  /* Program B (pid 300, also this UnixLib) takes it over.  */
  snprintf (F.var_owner, sizeof F.var_owner, "300"); dr_state = 1;
  int deact = dr_deactivations;
  __dsp_exit ();
  CHECK (dr_deactivations == deact && dr_state == 1, "A's exit leaves B's session alone");
  CHECK (strcmp (F.var_owner, "300") == 0, "and leaves B's claim");

  /* A writes again while B plays: it streams into B's session, as before,
     rather than take it back on every write (they would fight).  */
  fake_reset (); F.modules = 0; dr_state = 0; dr_deactivations = dr_activations = 0;
  dsp_open (O_WRONLY);
  __dspwrite (&FD, buf, sizeof buf);
  snprintf (F.var_owner, sizeof F.var_owner, "300");
  deact = dr_deactivations;
  int acts = dr_activations;
  __dspwrite (&FD, buf, sizeof buf);
  __dspwrite (&FD, buf, sizeof buf);
  CHECK (dr_deactivations == deact && dr_activations == acts && strcmp (F.var_owner, "300") == 0, "writing after a takeover doesn't fight over it");
  /* B stops: A's next write starts its own session again.  */
  dr_state = 0; F.var_set = 0;
  __dspwrite (&FD, buf, sizeof buf);
  CHECK (dr_activations == acts + 1 && F.var_set && strcmp (F.var_owner, "100") == 0, "once the other program stopped, A starts again and claims it");
  __dsp_exit ();
  CHECK (dr_state == 0 && !F.var_set, "exit stops it and removes the claim");

  /* The variable can't be read (deleted): assumed ours, as before.  */
  fake_reset (); F.modules = 0; dr_state = 0;
  dsp_open (O_WRONLY);
  __dspwrite (&FD, buf, sizeof buf);
  F.var_set = 0;
  __dsp_exit ();
  CHECK (dr_state == 0, "without the variable, exit stops it as before");
}

int main (void)
{
  test_two_opens ();
  test_read_ioctls ();
  test_fork_child_exit ();
  test_dr_takeover ();
  test_basic_s16_stereo ();
  test_formats ();
  test_partial_frames ();
  test_blocking_and_latency ();
  test_fragment_setting ();
  test_nonblock ();
  test_short_sound_starts ();
  test_reset ();
  test_stuck_stream ();
  test_exit ();
  test_dr ();
  printf ("%d checks, %d failed\n", checks, fails);
  return fails != 0;
}
