/* Host tests for UnixLib's /dev/midi (sound/midi.c) with a fake MIDISynth
   module and a fake MIDI module. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "swis.h"
#include "internal/dev.h"
#include "internal/unix.h"
#include <sys/soundcard.h>

char *program_invocation_short_name = "midiprog";
static int have_synth, have_hw, synth_opens, synth_closes, synth_resets, synth_limit;
static unsigned char synth_buf[4096], hw_buf[4096]; static int synth_len, hw_len;
static char synth_name[64], *env_midi;
static _kernel_oserror e = { 1, "x" };
int __ul_seterr (const _kernel_oserror *er, int en) { (void) er; errno = en; return -1; }
int fake_pid = 100;
int fake_getpid (void) { return fake_pid; }
static struct fake_callevery_block cb;
struct ul_global __ul_global = { 1, &cb };
static long now_cs; static int yields, frees_after = -1;
int fake_yield (void) { now_cs++; yields++; return 0; }
long fake_clock (void) { return now_cs++; }
char *fake_getenv (const char *n) { return strcmp (n, "UnixLib$MIDI") == 0 ? env_midi : NULL; }

/* SWI numbers the fake hands out: 0x100.. */
const _kernel_oserror *_swix (int swi, unsigned mask, ...)
{
  int in[10] = {0}, *out[10] = {0}, i;
  va_list ap; va_start (ap, mask);
  for (i = 0; i < 10; i++) if (mask & (1U << i)) in[i] = va_arg (ap, int);
  for (i = 0; i < 10; i++) if (mask & (1U << (31 - i))) out[i] = va_arg (ap, int *);
  va_end (ap);
  swi &= ~0x20000;
  if (swi == OS_SWINumberFromString)
    {
      const char *n = (const char *) (long) in[1];
      static const char *names[] = { "MIDISynth_Open", "MIDISynth_Close", "MIDISynth_Write", "MIDISynth_Reset", "MIDI_TxByte" };
      for (i = 0; i < 5; i++)
        if (strcmp (n, names[i]) == 0 && ((i < 4 && have_synth) || (i == 4 && have_hw)))
          { *out[0] = 0x100 + i; return NULL; }
      return &e;
    }
  switch (swi)
    {
    case 0x100: synth_opens++; snprintf (synth_name, 64, "%s", (char *) (long) in[1]); *out[0] = 0x55; return NULL;
    case 0x101: if (in[0] != 0x55) return &e; synth_closes++; return NULL;
    case 0x102:
      {
        if (in[0] != 0x55) return &e;
        if (frees_after >= 0 && yields >= frees_after) synth_limit = 1 << 20;
        int n = in[2] < synth_limit ? in[2] : synth_limit;
        memcpy (synth_buf + synth_len, (void *) (long) in[1], n); synth_len += n; *out[0] = n; return NULL;
      }
    case 0x103: synth_resets++; return NULL;
    case 0x104: hw_buf[hw_len++] = (unsigned char) in[0]; return NULL;
    }
  fprintf (stderr, "bad swi %x\n", swi); abort ();
}

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf ("FAIL %d: ", __LINE__); printf (__VA_ARGS__); printf ("\n"); } } while (0)
static struct __unixlib_fd_handle H; static struct __unixlib_fd FD;
static void *op (int mode) { FD.devicehandle = &H; FD.fflag = mode; return H.handle = __midiopen (&FD, "/dev/midi", mode); }
static void reset (void) { have_synth = have_hw = synth_opens = synth_closes = synth_resets = synth_len = hw_len = 0; synth_limit = 1 << 20; env_midi = NULL; }

/* 2026: a fork()/vfork() child exiting must not close the parent's
   connection (the child has a copy of, or shares, midi.c's state).  */
static void test_fork_child_exit (void)
{
  reset (); have_synth = 1; fake_pid = 100;
  CHECK (op (O_WRONLY) == (void *) 1 && synth_opens == 1, "fork test: open");
  fake_pid = 200;			/* the child */
  __midi_exit ();
  CHECK (synth_closes == 0, "a child's exit leaves the parent's connection open");
  fake_pid = 100;			/* back in the parent */
  CHECK (__midiwrite (&FD, "\x90\x40\x7f", 3) == 3, "parent can still write");
  __midi_exit ();
  CHECK (synth_closes == 1, "the parent's own exit closes it");
}

int main (void)
{
  test_fork_child_exit ();
  static unsigned char song[] = { 0xC0, 19, 0x90, 60, 100, 64, 100, 0xF0, 1, 2, 0xF7, 0x80, 60, 0 };
  reset (); have_synth = have_hw = 1;
  CHECK (op (O_WRONLY) == (void *) 1, "open");
  CHECK (synth_opens == 1 && strcmp (synth_name, "midiprog") == 0, "synth client named after the program");
  CHECK (__midiwrite (&FD, song, sizeof song) == (int) sizeof song && synth_len == (int) sizeof song && memcmp (synth_buf, song, sizeof song) == 0, "bytes pass through unchanged (running status + SysEx left to the module)");
  CHECK (hw_len == 0, "synth preferred over MIDI hardware");
  synth_limit = 5; synth_len = 0;
  CHECK (__midiwrite (&FD, song, sizeof song) == (int) sizeof song && synth_len == (int) sizeof song, "partial accepts are retried");
  /* Module full and nothing written: it used to return 0, which a
     write-all loop retries for ever.  */
  synth_limit = 0; errno = 0; FD.fflag = O_WRONLY | O_NONBLOCK;
  CHECK (__midiwrite (&FD, song, sizeof song) == -1 && errno == EAGAIN, "module full, non-blocking -> EAGAIN");
  FD.fflag = O_WRONLY; yields = 0; frees_after = 3; synth_len = 0;
  CHECK (__midiwrite (&FD, song, sizeof song) == (int) sizeof song && yields == 3, "module full, blocking -> waits for room (%d yields)", yields);
  synth_limit = 0; frees_after = -1; now_cs = 0; errno = 0;
  CHECK (__midiwrite (&FD, song, sizeof song) == -1 && errno == EIO && now_cs > 200, "stuck module -> EIO after 2 s");
  cb.pthread_worksemaphore = 1; yields = 0; now_cs = 0;
  __midiwrite (&FD, song, sizeof song);
  CHECK (yields == 0, "no pthread_yield with thread switching held off");
  cb.pthread_worksemaphore = 0;
  synth_limit = 1 << 20;
  CHECK (__midiioctl (&FD, SNDCTL_SEQ_RESET, NULL) == 0 && synth_resets == 1, "SEQ_RESET -> MIDISynth_Reset");
  CHECK (op (O_WRONLY) == (void *) 1 && synth_opens == 1, "second open shares the connection");
  __midiclose (&FD);
  CHECK (synth_closes == 0, "still one open");
  __midiclose (&FD);
  CHECK (synth_closes == 1, "last close disconnects");
  __midi_exit ();
  CHECK (synth_closes == 1, "exit after close does nothing");

  reset (); have_synth = 1;
  op (O_WRONLY); __midi_exit ();
  CHECK (synth_closes == 1, "exit without close disconnects (no hanging notes)");

  reset (); have_hw = 1;
  CHECK (op (O_WRONLY) == (void *) 1, "hardware fallback");
  CHECK (__midiwrite (&FD, song, 5) == 5 && hw_len == 5 && memcmp (hw_buf, song, 5) == 0, "MIDI_TxByte per byte");
  hw_len = 0; __midiclose (&FD);
  CHECK (hw_len == 16 * 6 && hw_buf[0] == 0xB0 && hw_buf[1] == 123 && hw_buf[4] == 121, "close sends all notes off / reset controllers");
  reset (); have_hw = 1;
  op (O_WRONLY); __midiclose (&FD);
  CHECK (hw_len == 0, "close without writing leaves the hardware alone (%d bytes sent)", hw_len);

  reset (); have_synth = have_hw = 1; env_midi = "MIDI";
  op (O_WRONLY); __midiwrite (&FD, song, 3);
  CHECK (synth_opens == 0 && hw_len == 3, "UnixLib$MIDI=MIDI forces hardware");
  __midiclose (&FD);
  reset (); have_hw = 1; env_midi = "MIDISynth"; errno = 0;
  CHECK (op (O_WRONLY) == (void *) -1 && errno == ENODEV, "UnixLib$MIDI=MIDISynth without the module -> ENODEV");
  reset (); errno = 0;
  CHECK (op (O_WRONLY) == (void *) -1 && errno == ENODEV, "nothing loaded -> ENODEV");
  reset (); have_synth = 1; errno = 0;
  CHECK (op (O_RDONLY) == (void *) -1 && errno == ENODEV, "no MIDI in");
  reset ();
  __midi_exit ();
  CHECK (synth_closes == 0 && hw_len == 0, "exit of a program that never opened it does nothing");
  printf ("%d checks, %d failed\n", checks, fails);
  return fails != 0;
}
