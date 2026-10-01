/*
 * /dev/midi: OSS-style raw MIDI output for RISC OS.
 *
 * Programs write raw MIDI bytes (note on/off, program change, controllers,
 * running status, SysEx) to /dev/midi and they play at once, as with a
 * MIDI port on Linux.
 *
 * Where the bytes go, first one found:
 *   1. the MIDISynth module (a General MIDI software synthesiser shared by
 *      every program; SWIs MIDISynth_Open/Write/Reset/Close, see
 *      docs/MIDISYNTH-MODULE.md in riscos-unixlib);
 *   2. the RISC OS MIDI module (external MIDI hardware or USB MIDI),
 *      byte by byte with MIDI_TxByte.
 * UnixLib$MIDI set to "MIDISynth" or "MIDI" allows only that one.
 * If neither is loaded, opening /dev/midi fails with ENODEV.
 *
 * The SWIs are looked up by name when the device is opened, so no SWI
 * numbers are built in.  Reading gives end of file: there is no MIDI in.
 *
 * Copyright (c) 2026 UnixLib Developers
 * Written by Andrew Youll.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <swis.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/soundcard.h>

#include <internal/os.h>
#include <internal/dev.h>
#include <internal/unix.h>

enum { MIDI_NONE, MIDI_SYNTH, MIDI_HW };

static int midi_kind = MIDI_NONE;
static int midi_opens;			/* open /dev/midi descriptors */
static int swi_open, swi_close, swi_write, swi_reset, swi_txbyte;
static int synth_handle;
/* 2026: the process that made the connection.  A fork()/vfork() child has
   a copy of (or shares) these variables but must not close the parent's
   connection when it exits.  */
static pid_t midi_owner_pid;
/* 2026: something was sent to MIDI hardware since the connection was made
   (close only resets the 16 channels then).  */
static int hw_written;

/* Avoid warnings, but don't advertise.  */
void __midi_exit (void);

static int
swi_number (const char *name)
{
  int n;
  if (_swix (OS_SWINumberFromString, _IN(1) | _OUT(0), name, &n) != NULL)
    return 0;
  return n | (1 << 17);			/* X bit */
}

static int
find_synth (void)
{
  swi_open = swi_number ("MIDISynth_Open");
  swi_close = swi_number ("MIDISynth_Close");
  swi_write = swi_number ("MIDISynth_Write");
  swi_reset = swi_number ("MIDISynth_Reset");
  return swi_open && swi_close && swi_write;
}

static int
find_hw (void)
{
  swi_txbyte = swi_number ("MIDI_TxByte");
  return swi_txbyte != 0;
}

/* All notes off and reset controllers on every channel.  */
static void
hw_panic (void)
{
  int ch;
  for (ch = 0; ch < 16; ch++)
    {
      _swix (swi_txbyte, _IN(0), 0xB0 | ch);
      _swix (swi_txbyte, _IN(0), 123);	/* all notes off */
      _swix (swi_txbyte, _IN(0), 0);
      _swix (swi_txbyte, _IN(0), 0xB0 | ch);
      _swix (swi_txbyte, _IN(0), 121);	/* reset all controllers */
      _swix (swi_txbyte, _IN(0), 0);
    }
}

void *
__midiopen (struct __unixlib_fd *fd, const char *file, int mode)
{
  const char *want = getenv ("UnixLib$MIDI");
  int only_synth = want && strcasecmp (want, "MIDISynth") == 0;
  int only_hw = want && strcasecmp (want, "MIDI") == 0;
  const _kernel_oserror *err;
  const char *name = program_invocation_short_name;

  (void) fd;
  (void) file;
  if ((mode & O_ACCMODE) == O_RDONLY)
    return (void *) __set_errno (ENODEV);	/* no MIDI in */

  /* One connection per program, shared by its descriptors.  */
  if (midi_kind != MIDI_NONE)
    {
      midi_opens++;
      return (void *) 1;
    }

  if (!only_hw && find_synth ())
    {
      err = _swix (swi_open, _INR(0,1) | _OUT(0), 0,
		   (name && *name) ? name : "UnixLib", &synth_handle);
      if (err == NULL)
	{
	  midi_kind = MIDI_SYNTH;
	  midi_opens = 1;
	  midi_owner_pid = getpid ();
	  return (void *) 1;
	}
      if (only_synth)
	return (void *) __ul_seterr (err, EOPSYS);
    }
  if (!only_synth && find_hw ())
    {
      midi_kind = MIDI_HW;
      midi_opens = 1;
      midi_owner_pid = getpid ();
      return (void *) 1;
    }
  return (void *) __set_errno (ENODEV);
}

static void
midi_disconnect (void)
{
  if (midi_kind == MIDI_SYNTH)
    _swix (swi_close, _IN(0), synth_handle);
  else if (midi_kind == MIDI_HW && hw_written)
    hw_panic ();
  midi_kind = MIDI_NONE;
  midi_opens = 0;
  synth_handle = 0;
  hw_written = 0;
}

int
__midiclose (struct __unixlib_fd *fd)
{
  (void) fd;
  if (midi_kind != MIDI_NONE && --midi_opens <= 0)
    midi_disconnect ();
  return 0;
}

/* Let other threads run while waiting, when that's allowed (pthread_yield
   is a fatal error with thread switching held off).  */
static void
midi_yield (void)
{
  if (__ul_global.pthread_system_running
      && __ul_global.pthread_callevery_rma->pthread_worksemaphore == 0)
    pthread_yield ();
}

int
__midiwrite (struct __unixlib_fd *fd, const void *data, int nbyte)
{
  const _kernel_oserror *err;
  const unsigned char *p = data;
  int i;

  switch (midi_kind)
    {
    case MIDI_SYNTH:
      {
	int done = 0, n;
	clock_t since = clock ();
	while (done < nbyte)
	  {
	    err = _swix (swi_write, _INR(0,2) | _OUT(0), synth_handle,
			 p + done, nbyte - done, &n);
	    if (err)
	      return done ? done : __ul_seterr (err, EOPSYS);
	    if (n > 0)
	      {
		done += n;
		since = clock ();
		continue;
	      }
	    /* The module is full.  Some bytes written: a short write.
	       2026: none written used to return 0, which a write-all loop
	       retries for ever; now non-blocking writes fail with EAGAIN,
	       and blocking ones wait for room (up to 2 s, then EIO).  */
	    if (done)
	      break;
	    if (fd->fflag & O_NONBLOCK)
	      return __set_errno (EAGAIN);
	    if (clock () - since > 200)
	      return __set_errno (EIO);
	    midi_yield ();
	  }
	return done;
      }

    case MIDI_HW:
      hw_written = 1;
      for (i = 0; i < nbyte; i++)
	if ((err = _swix (swi_txbyte, _IN(0), p[i])) != NULL)
	  return i ? i : __ul_seterr (err, EOPSYS);
      return nbyte;
    }
  return __set_errno (EBADF);
}

int
__midiioctl (struct __unixlib_fd *fd, unsigned long request, void *arg)
{
  (void) fd;
  (void) arg;
  switch (request & 0xffff)
    {
    case SNDCTL_SEQ_RESET & 0xffff:
    case SNDCTL_SEQ_PANIC & 0xffff:
      if (midi_kind == MIDI_SYNTH && swi_reset)
	_swix (swi_reset, _IN(0), synth_handle);
      else if (midi_kind == MIDI_HW)
	hw_panic ();
      return 0;

    case SNDCTL_SEQ_SYNC & 0xffff:
      return 0;			/* bytes play as they are written */
    }
  return __set_errno (EINVAL);
}

/* Called from _exit(): don't leave notes hanging if the program didn't
   close /dev/midi (or crashed).  Nothing happens if it never opened it.  */
void
__midi_exit (void)
{
  if (midi_kind != MIDI_NONE && midi_owner_pid == getpid ())
    midi_disconnect ();
}
