/*
 * Implement OSS/soundcard.h support
 *
 * Written by Peter Naulls <peter@chocky.org>
 * and Christopher Martin <belles@internode.on.net>
 *
 * This is essentially an emulation of the /dev/dsp device found under Linux
 * and other systems and the OSS interface.
 *
 * To achieve this, we make use of the DigitalRenderer module, which we feed
 * sample data we're given via the device write function.  Originally, we
 * used the DRender: device provided by the module, but that proved problematic
 * because it would not start playing until its buffer was full, and too
 * small buffers meant playing glitches.  Using the streaming interface
 * is much more responsive.
 *
 * We support 16-bit and 8-bit ulaw in stereo and mono.
 *
 * 2026: SharedSoundBuffer / StreamManager output when those modules are
 * loaded (mixes with other programs; see the second half of this file),
 * and DigitalRenderer is no longer stopped by programs that didn't use it.
 *
 * Copyright (c) 2004-2012 UnixLib Developers
 */

#include <swis.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/soundcard.h>
#include <strings.h>
#include <time.h>

#include <internal/os.h>
#include <internal/dev.h>
#include <internal/swiparams.h>
#include <pthread.h>

#include "DRender.h"

#define DRENDERER_BUFFER_SIZE 512
#define DRENDERER_CSEC_TO_BUFFER 500

static int dr_channels  = 2;
static int dr_format    = AFMT_S16_LE;
static int dr_frequency = 44100;

static int dr_buffers = 0;     /* Number of buffers in use */
static int dr_fragscale = 1;
static int dr_owner = 0;       /* Non-zero once this program activated it */

/* 2026: the process whose SharedSoundBuffer stream or DigitalRenderer
   session this is.  A fork()/vfork() child has a copy of (or shares) these
   variables but not the session: its _exit() must leave the parent's sound
   alone.  */
static pid_t dsp_owner_pid;

/* 2026: DigitalRenderer has one user at a time and doesn't say who.  A
   program that activates it puts its pid in this system variable; one that
   finds another pid there has been taken over and must not deactivate the
   new owner's session.  A program that doesn't set it (one built with an
   older UnixLib, or not using UnixLib) can't be detected: after it takes
   over, the variable still holds this program's pid, and this program
   stops DigitalRenderer when it closes or exits, as it always did.  If the
   variable can't be read, it is assumed to be this program's.  */
#define DR_OWNER_VAR "UnixLib$DSPOwner"

/* Static, like the rest of this device's state (and so the host tests can
   pass it through their 32-bit fake SWI registers).  */
static char dr_var_buf[16];

static void
dr_claim (void)
{
  char *buf = dr_var_buf;
  int len = snprintf (buf, sizeof dr_var_buf, "%d", (int) getpid ());
  _swix (OS_SetVarVal, _INR(0,4), DR_OWNER_VAR, buf, len, 0, 0);
}

static void
dr_unclaim (void)
{
  _swix (OS_SetVarVal, _INR(0,4), DR_OWNER_VAR, NULL, -1, 0, 0);
}

/* Set when another program (with this UnixLib) took DigitalRenderer over
   from this one.  */
static int dr_taken_over;

/* Non-zero if this program activated DigitalRenderer and nobody has taken
   it over since.  */
static int
dr_is_ours (void)
{
  char *buf = dr_var_buf;
  int len = 0;

  if (!dr_owner || dsp_owner_pid != getpid ())
    return 0;
  if (_swix (OS_ReadVarVal, _INR(0,4) | _OUT(2), DR_OWNER_VAR, buf,
	     (int) sizeof dr_var_buf - 1, 0, 0, &len) != NULL)
    return 1;			/* no variable: assume ours, as before */
  buf[len < 0 ? 0 : len] = '\0';
  if (atoi (buf) == (int) getpid ())
    return 1;
  dr_owner = 0;			/* taken over by another program */
  dr_taken_over = 1;
  return 0;
}

/* Stop DigitalRenderer if it is ours.  */
static const _kernel_oserror *
dr_release (void)
{
  const _kernel_oserror *err;

  if (!dr_is_ours ())
    {
      dr_owner = 0;
      return NULL;
    }
  dr_owner = 0;
  dr_unclaim ();
  err = DRender_Deactivate ();
  DRender_NumBuffers (0);
  return err;
}


/* Close /dev/dsp upon exit if it's still open.  This is mostly
   to ensure that DigitalRenderer is in a good state after
   an Alt-Break or an exception.  */
static void
dr_exit (void)
{
  /* 2026: only if this program started DigitalRenderer.  _exit() calls this
     in every program, so it used to stop the sound of whichever other
     program was playing whenever any UnixLib program quit.  */
  (void) dr_release ();
}


static const _kernel_oserror *
check_state (int *state)
{
  if ((*state = DRender_ReadState ()) == -1)
    {
      const _kernel_oserror *err;
#if 0 /* Better to use OS_Module directly to try (re)loading the module */
      if (   (err = SWI_OS_CLI ("RMEnsure DigitalRenderer 0.55 "
                                "RMLoad System:Modules.DRenderer")) != NULL
          || (err = SWI_OS_CLI ("RMEnsure DigitalRenderer 0.55 Error 16_10F "
                                "Sound support requires DigitalRenderer 0.55 "
                                "or newer")) != NULL
         )
        return err;
#else
      if ((err = DRender_LoadModule (NULL)) != NULL)
        return err;
#endif
      *state = DRender_ReadState (); /* Is there the possibility of an error
                                        returning -1 at this moment? */
    }
  return NULL;
}


static const _kernel_oserror *
activate_defaults (void)
{
  const _kernel_oserror *err;
  int n;

  /* 2026: the buffer settings are applied here, when this program starts
     playing, not when it opens /dev/dsp or changes a setting: those used to
     reconfigure (and stop) DigitalRenderer while another program was
     playing through it.  */
  if ((n = DRender_NumBuffers (dr_buffers)) > 0)
    dr_buffers = n;
  /* Set to fill with zero when out of data,
     but no upcalls nor blocking on overflow */
  DRender_StreamFlags (DRStream_OverrunNull, 0);

  if (dr_format == AFMT_S16_LE)
    {
      if ((err = DRender_Activate16 (dr_channels, DRENDERER_BUFFER_SIZE,
                                     dr_frequency, DRActivate_Restore)) == NULL)
        {
          /* The SWI calls in this block only work after DRender_Activate16 */
          int freq = DRender_GetFrequency ();
          if (freq > 0)
            dr_frequency = freq;
          /* Stereo samples intended for /dev/dsp will be in left-right order */
          DRender_SampleFormat (DRFORMAT_S16LR);
        }
    }
  else
    {
      err = DRender_Activate (dr_channels, DRENDERER_BUFFER_SIZE,
                              1e6 / dr_frequency, NULL);
    }
  if (err == NULL)
    {
      dr_owner = 1;
      dr_taken_over = 0;
      dsp_owner_pid = getpid ();
      dr_claim ();
    }
  return err;
}


static const _kernel_oserror *
set_defaults (struct __unixlib_fd *fd, int channels, int format, int frequency,
              int buffers)
{
  const _kernel_oserror *err;
  int old_state;

  if ((err = check_state (&old_state)) != NULL)
    return err;

  /* Only stop DigitalRenderer if this program is the one playing; the new
     settings take effect at the next write.  */
  if (dr_is_ours () && (old_state & DRState_Active) && (old_state != -1))
    {
      dr_unclaim ();
      DRender_Deactivate ();
      dr_owner = 0;
    }

  if (channels)
    dr_channels = channels;
  if (frequency)
    dr_frequency = frequency;
  if (format)
    dr_format = format;

  if (!buffers)
    buffers = dr_buffers;
  if (!buffers)
    {
      /* This still isn't quite correct as the buffers should
         really change if the frequency is changed, however as
         the default will buffer for a relatively long time
         anyway I don't think it matters. */
      buffers = (int)( (double)DRENDERER_CSEC_TO_BUFFER
                     / ( (double)(DRENDERER_BUFFER_SIZE * 100)
                       / dr_frequency
                       )
                     )
                + 1;
    }
  dr_buffers = buffers;

#if 0 /* Defer re-activating until samples are written to the stream */
  if ((old_state & DRState_Active) && (old_state != -1))
    return activate_defaults ();
#endif

  return NULL;
}


static void *
dr_open (struct __unixlib_fd *fd, const char *file, int mode)
{
  const _kernel_oserror *err = NULL;
  dr_fragscale = 1;
#if 0
  /*
    This is the sensible place to call activate_defaults() but the results of
    doing so are terrible(!) for FFplay and Mplayer on RISC OS 5.18 for
    BeagleBoard. Because applications open /dev/dsp and THEN start mucking about
    with ioctls on it, the BeagleBoard is continually deactivating and
    reactivating the sound system, taking forever and emitting high-pitched
    whistles. So instead, we defer the call to activate_defaults() until an
    attempt to write to the stream.
  */
  {
    int state;
    if (   (err = set_defaults (fd, 2, AFMT_S16_LE, 44100, 0)) == NULL
        && (err = check_state (&state)) == NULL
        && !(state & DRState_Active)
       )
      err = activate_defaults ();
  }
#else
  /*
    Set the defaults but don't activate them yet.
  */
  err = set_defaults (fd, 2, AFMT_S16_LE, 44100, 0);
#endif
  if (err)
    return (void *) __ul_seterr (err, EOPSYS);
  return (void *) 1; /* Dummy value */
}


static int
dr_close (struct __unixlib_fd *fd)
{
  if (fd->devicehandle->handle)
    {
      /* Only if this program is still the one playing: leave others'
         sound alone.  */
      const _kernel_oserror *err = dr_release ();
      if (err != NULL)
        return __ul_seterr (err, EOPSYS);
    }
  return 0;
}


__off_t
__dsplseek (struct __unixlib_fd *fd, __off_t lpos, int whence)
{
  /* Do nothing, just so programs don't complain.  */
  return 0;
}


static int
dr_write (struct __unixlib_fd *fd, const void *data, int nbyte)
{
  const _kernel_oserror *err;

#if 0
  /*
    Normally, one would simply assume that the stream was already actived by
    __dspopen(). That would be sensible. Normally, we wouldn't waste time on any
    kind of status checking or activation at this point. But that was before
    RISC OS 5.18 landed on the BeagleBoard...
  */
#else
  /*
    On the plus side, if DigitalRenderer gets deactivated somehow, this will try
    to reactivate it again... A small plus...
  */
  {
    int state;
    if ((err = check_state (&state)) == NULL)
      {
        int ours = dr_is_ours ();
        if (!(state & DRState_Active))
          err = activate_defaults ();	/* nobody is playing */
        else if (!ours && !dr_taken_over)
          {
            /* Someone else is playing: DigitalRenderer has one user at a
               time, so take it over (as before), but only now that this
               program has something to play.  */
            DRender_Deactivate ();
            err = activate_defaults ();
          }
        /* 2026: else either ours, or another program took it over from
           this one: then stream into its session, as before, rather than
           take it back on every write (two programs would fight over it
           and both stutter).  Its exit is its business.  */
      }
    if (err)
      return __ul_seterr (err, EOPSYS);
  }
#endif

  int buffer_byte_size = DRENDERER_BUFFER_SIZE * dr_channels;
  if (dr_format == AFMT_S16_LE)
    buffer_byte_size <<= 1;

  int left = nbyte;
  while (left > 0)
    {
      /* 2026: wait for room with __pthread_held_wait (a yield with
	 switching held off, as through fwrite, was a fatal error), and
	 give up after 2 s with nothing played, as the SharedSoundBuffer
	 path does (it waited for ever).  */
      int waiting, last_waiting = -1;
      clock_t since = clock ();
      while ((waiting = DRender_StreamStatistics ()) >= dr_buffers)
        {
	  if (waiting != last_waiting)
	    {
	      last_waiting = waiting;
	      since = clock ();
	    }
	  else if (clock () - since > 200)
	    {
	      int done = nbyte - left;
	      return done ? done : __set_errno (EIO);
	    }
	  (void) __pthread_held_wait (0);
        }

      void *next = (void*)((size_t)data + nbyte - left);
      int towrite = ((left < buffer_byte_size) ? left : buffer_byte_size);
      /* Size is in samples, counting left and right as separate samples
         so it is always divided by 2 for 16 bit samples regardless of
         the number of channels */
      if ((err = (dr_format == AFMT_S16_LE)
                 ? DRender_Stream16BitSamples (next, towrite >> 1)
                 : DRender_StreamSamples (next, towrite)) != NULL)
        /* 2026: what was already written counts (a short write).  */
        return nbyte - left > 0 ? nbyte - left : __ul_seterr (err, EOPSYS);

      left -= buffer_byte_size;
    }

  return nbyte;
}


static int
dr_ioctl (struct __unixlib_fd *fd, unsigned long request, void *arg)
{
  request &= 0xffff;

  /* Do nothing */
  if (request == (SNDCTL_DSP_RESET & 0xffff)
      || request == (SNDCTL_DSP_SYNC & 0xffff))
    return 0;

  if (!arg)
    return __set_errno (EINVAL);

  switch (request)
    {
      case SNDCTL_DSP_SPEED & 0xffff:
        {
          if (!set_defaults (fd, 0, 0, *((int *)arg), 0))
            return 0;

          return __set_errno (EINVAL);
        }

      case SNDCTL_DSP_SETFMT & 0xffff:
        {
          int format = *((int *)arg);

          /* DigitalRenderer supports 8-bit ulaw and 16-bit signed linear */
          if ((format == AFMT_MU_LAW || format == AFMT_S16_LE)
              && !set_defaults (fd, 0, format, 0, 0))
            return 0;

          return __set_errno (EINVAL);
        }

      case SNDCTL_DSP_STEREO & 0xffff:
        {
          if (!set_defaults (fd, (*((int *)arg) == 1) ? 2 : 1, 0, 0, 0))
            return 0;

          return __set_errno (EINVAL);
        }

      case SNDCTL_DSP_GETBLKSIZE & 0xffff:
        {
          int blksize = DRENDERER_BUFFER_SIZE * dr_channels;

          /* 2026 (audit): a buffer is twice as many bytes for 16-bit
             samples, as dr_write has it; this doubled for mu-law.  And
             it's the fragment size, as GETOSPACE reports it (OSS
             programs take it as that): dr_fragscale buffers.  */
          if (dr_format == AFMT_S16_LE)
            blksize <<= 1;
          blksize *= dr_fragscale;
          *((int *)arg) = blksize;

          return 0;
        }

      case SNDCTL_DSP_CHANNELS & 0xffff:
        {
          if (!set_defaults (fd, *((int *)arg), 0, 0, 0))
            return 0;

          return __set_errno (EINVAL);
        }

      case SNDCTL_DSP_SETFRAGMENT & 0xffff:
        {
          int fragspec  = *((int *)arg);
          int fragments = (fragspec >> 16) & 0x7fff;
          int shift = fragspec & 0xffff;
          int fragsize, unlimited, max_buffers;

          /* 2026 (audit): limits like those S10 gave the SharedSoundBuffer
             path.  The exponent is checked before shifting (1 << 40 is
             undefined), and the total is kept to about 2 s: "no limit"
             (0x7fff, the usual 0x7fff000a) asked DigitalRenderer for
             32767 buffers; now it (and 0) means the most allowed.  */
          if (shift > 16)
            shift = 16;
          if (shift < 7)
            shift = 7;
          fragsize = 1 << shift;
          unlimited = (fragments == 0 || fragments == 0x7fff);
          if (fragments < 2)
            fragments = 2;

          /* fragsize is in bytes, but the buffer size used
             by DigitalRenderer is in samples */
          dr_fragscale = (fragsize / (DRENDERER_BUFFER_SIZE * dr_channels));
          if (dr_format == AFMT_S16_LE)
            dr_fragscale >>= 1;

          if (dr_fragscale == 0)
            dr_fragscale = 1;
          max_buffers = dr_frequency * 2 / DRENDERER_BUFFER_SIZE;
          if (max_buffers < 2)
            max_buffers = 2;
          /* At least two fragments within the limit (a fragment bigger
             than half of it made GETOSPACE report 0 fragments).  */
          if (dr_fragscale > max_buffers / 2)
            dr_fragscale = max_buffers / 2 > 0 ? max_buffers / 2 : 1;
          fragments *= dr_fragscale;
          if (unlimited || fragments > max_buffers)
            fragments = max_buffers;

          if (!set_defaults (fd, 0, 0, 0, fragments))
            return 0;

          return __set_errno (EINVAL);
        }

      case SNDCTL_DSP_GETFMTS & 0xffff:
        *((int *)arg) = AFMT_MU_LAW | AFMT_S16_LE;
        return 0;

      case SNDCTL_DSP_GETOSPACE & 0xffff:
        {
          int waiting = DRender_StreamStatistics ();
          if (waiting < 0)
            waiting = 0;

          struct audio_buf_info *info = arg;
          info->fragments = (dr_buffers - waiting) / dr_fragscale;
          info->fragstotal = dr_buffers / dr_fragscale;
          info->fragsize = DRENDERER_BUFFER_SIZE * dr_fragscale * dr_channels;
          info->bytes = (dr_buffers - waiting) * DRENDERER_BUFFER_SIZE * dr_channels;
          if (dr_format == AFMT_S16_LE)
            {
              info->fragsize <<= 1;
              info->bytes <<= 1;
            }
          return 0;
        }

      case SNDCTL_DSP_GETCAPS & 0xffff:
        *((int *)arg) = 0; /*DSP_CAP_REALTIME;*/
        return 0;

      case SNDCTL_DSP_GETOPTR & 0xffff:
        {
          struct count_info *info = arg;

          /* TODO: add these values */
          info->bytes   = 0;
          info->blocks  = 0;
          info->ptr     = 0;
          return 0;
        }
    }

  return __set_errno (EINVAL);
}


/* ------------------------------------------------------------------------
   2026: SharedSoundBuffer / StreamManager output.

   SharedSoundBuffer (RISC OS 5, SharedSound 1.07+, StreamManager 0.03+,
   SharedSoundBuffer 0.07+) plays a stream of 16-bit stereo samples at any
   rate through SharedSound, so it mixes with every other program making
   sound and resamples to the hardware rate.  StreamManager copies each
   block into its own memory, so this is plain user mode code with no
   interrupt handlers.  Same interface as RDPClient and SDL2's RISC OS
   audio driver.

   Used when those modules are loaded, else DigitalRenderer as before.
   UnixLib$DSP set to "DigitalRenderer" forces the old path, and to
   "SharedSound" refuses to fall back.

   Everything the program writes is converted to S16LE stereo here, so all
   the usual OSS formats work in mono or stereo.  The OSS "fragment" sizes
   the program sees are in its own format; internally everything is kept in
   output bytes (4 per frame).
   ------------------------------------------------------------------------ */

#define XSharedSoundBuffer_OpenStream         0x75FC0
#define XSharedSoundBuffer_CloseStream        0x75FC1
#define XSharedSoundBuffer_Volume             0x75FC4
#define XSharedSoundBuffer_SampleRate         0x75FC5
#define XSharedSoundBuffer_Pause              0x75FC9
#define XSharedSoundBuffer_ReturnStreamHandle 0x75FCE
#define XStreamManager_AddBlock               0x77282
#define XStreamManager_SetBuffer              0x77287
#define XStreamManager_BufferStats            0x77288

#define SSB_FORMATS (AFMT_S16_LE | AFMT_S16_BE | AFMT_U8 | AFMT_S8 | AFMT_MU_LAW)
#define SSB_OUT_FRAG_DEFAULT 4096	/* output bytes: 1024 frames */
#define SSB_NFRAGS_DEFAULT   8		/* ~190 ms at 44.1 kHz */
#define SSB_CONV_SIZE        8192	/* conversion buffer, output bytes */

/* 2026: <sys/soundcard.h> encodes requests one of two ways: with its own
   _SIOR (direction "out" = 0x20000000) or, when <sys/ioctl.h> was included
   first, with _IOR (0x40000000).  A program may use either, so the
   read-only requests told apart by their full value are checked in both.
   (With the second encoding, a read request has the same value as the
   first encoding's _SIOW write request: only treat a value as a read
   where no write request of that number is handled.)  */
#define OSS_READ(enc, n)  ((unsigned long) ((enc) | ((sizeof (int) & 0x1fff) << 16) | ('P' << 8) | (n)))
#define OSS_IS_READ(req, n)  ((req) == OSS_READ (0x20000000, n) || (req) == OSS_READ (0x40000000, n))

enum { BACKEND_NONE, BACKEND_DR, BACKEND_SSB };
static int dsp_backend = BACKEND_NONE;
/* 2026: /dev/dsp descriptors open in this program (dup()s share one).  The
   device state is per program, so a second open shares the first one's
   stream and settings; it used to close the first one's stream and reset
   its settings, and its close switched the first one to DigitalRenderer
   (SDL probes the device like this).  */
static int dsp_opens;
static pid_t dsp_open_pid;	/* the process that opened it */

static struct
{
  int channels, format, rate;
  int frag_out;			/* fragment size, output bytes */
  int cap_out;			/* how much may be queued, output bytes */
  int handle, stream;		/* 0 while the stream isn't open */
  int started;			/* unpaused */
  unsigned int played_base;	/* for GETOPTR */
  unsigned int last_frag;
  unsigned char carry[4];	/* incomplete input frame */
  int carry_len;
  unsigned char *conv;		/* SSB_CONV_SIZE output bytes; 2026:
				   allocated on first use rather than
				   static in every program */
} ssb;

static int
swi_exists (const char *name)
{
  int n;
  return _swix (OS_SWINumberFromString, _IN(1) | _OUT(0), name, &n) == NULL;
}

static int
ssb_available (void)
{
  return swi_exists ("SharedSoundBuffer_OpenStream")
	 && swi_exists ("StreamManager_AddBlock");
}

static int
ssb_in_frame (void)
{
  int bps = (ssb.format == AFMT_S16_LE || ssb.format == AFMT_S16_BE) ? 2 : 1;
  return bps * ssb.channels;
}

/* Output bytes <-> the program's bytes.  */
static int
ssb_to_client (int out)
{
  return (out / 4) * ssb_in_frame ();
}

/* Bytes queued and not yet played (output bytes), or -1.  */
static int
ssb_queued (unsigned int *played)
{
  int added, done;
  if (!ssb.handle
      || _swix (XStreamManager_BufferStats, _IN(0) | _OUTR(0,1),
		ssb.stream, &added, &done) != NULL)
    return -1;
  if (played)
    *played = (unsigned int) done;
  return added - done;
}

static void
ssb_pause (int pause)
{
  if (ssb.handle)
    _swix (XSharedSoundBuffer_Pause, _INR(0,1), ssb.handle, pause ? 0 : 1);
  ssb.started = !pause;
}

static void
ssb_close_stream (void)
{
  if (ssb.handle)
    _swix (XSharedSoundBuffer_CloseStream, _IN(0), ssb.handle);
  ssb.handle = ssb.stream = 0;
  ssb.started = 0;
  ssb.carry_len = 0;
  /* 2026: GETOPTR after RESET counts from zero again (it returned the
     closed stream's count minus nothing sensible).  */
  ssb.played_base = ssb.last_frag = 0;
}

static const _kernel_oserror *
ssb_open_stream (void)
{
  const _kernel_oserror *err;
  const char *name = program_invocation_short_name;

  if (ssb.handle)
    return NULL;
  /* R0 bit 1: R2 is the usual block size.  */
  err = _swix (XSharedSoundBuffer_OpenStream, _INR(0,2) | _OUT(0), 2,
	       (name && *name) ? name : "UnixLib", ssb.frag_out, &ssb.handle);
  if (err)
    {
      ssb.handle = 0;
      return err;
    }
  err = _swix (XSharedSoundBuffer_ReturnStreamHandle, _IN(0) | _OUT(0),
	       ssb.handle, &ssb.stream);
  if (err)
    {
      _swix (XSharedSoundBuffer_CloseStream, _IN(0), ssb.handle);
      ssb.handle = 0;
      return err;
    }
  /* Room for well over what we queue, so AddBlock never refuses.  */
  _swix (XStreamManager_SetBuffer, _INR(0,1), ssb.stream,
	 ssb.cap_out * 2 + SSB_CONV_SIZE * 2);
  _swix (XSharedSoundBuffer_SampleRate, _INR(0,1), ssb.handle,
	 ssb.rate * 1024);
  _swix (XSharedSoundBuffer_Volume, _INR(0,1), ssb.handle, 0xFFFFFFFF);
  ssb.played_base = ssb.last_frag = 0;
  dsp_owner_pid = getpid ();
  /* Paused until a fragment is queued, so it doesn't start by running
     dry.  */
  ssb_pause (1);
  return NULL;
}

static void
ssb_set_defaults (void)
{
  ssb.channels = 2;
  ssb.format = AFMT_S16_LE;
  ssb.rate = 44100;
  ssb.frag_out = SSB_OUT_FRAG_DEFAULT;
  ssb.cap_out = SSB_OUT_FRAG_DEFAULT * SSB_NFRAGS_DEFAULT;
  ssb.carry_len = 0;
}

/* Wait a little for the queue to drain.  Returns 0 if it's stuck (nothing
   played for two seconds while unpaused).  */
static int
ssb_wait (int *last_queued, clock_t *since)
{
  int q = ssb_queued (NULL);
  if (!ssb.started)
    ssb_pause (0);		/* waiting for room: it must be playing */
  if (q < 0)
    return 0;
  if (q != *last_queued)
    {
      *last_queued = q;
      *since = clock ();
    }
  else if (clock () - *since > 200)
    return 0;
  /* 2026: through stdio the hold is nested and yielding was fatal; with
     a nested hold this spins until the queue drains (or the 2 s limit).  */
  (void) __pthread_held_wait (0);
  return 1;
}

static inline short
ulaw_to_s16 (unsigned char u)
{
  int t;
  u = ~u;
  t = ((u & 0x0f) << 3) + 0x84;
  t <<= (u & 0x70) >> 4;
  return (short) ((u & 0x80) ? (0x84 - t) : (t - 0x84));
}

/* Convert FRAMES input frames at IN to S16LE stereo at OUT.  */
static void
ssb_convert (const unsigned char *in, short *out, int frames)
{
  int i, c, ch = ssb.channels;
  for (i = 0; i < frames; i++)
    {
      short v[2] = { 0, 0 };
      for (c = 0; c < ch; c++)
	{
	  switch (ssb.format)
	    {
	    case AFMT_S16_LE: v[c] = (short) (in[0] | (in[1] << 8)); in += 2; break;
	    case AFMT_S16_BE: v[c] = (short) (in[1] | (in[0] << 8)); in += 2; break;
	    case AFMT_U8:     v[c] = (short) ((in[0] - 128) << 8); in++; break;
	    case AFMT_S8:     v[c] = (short) (((signed char) in[0]) << 8); in++; break;
	    default:          v[c] = ulaw_to_s16 (in[0]); in++; break;
	    }
	}
      *out++ = v[0];
      *out++ = (ch == 2) ? v[1] : v[0];
    }
}

static int
ssb_add (const void *block, int len)
{
  return _swix (XStreamManager_AddBlock, _INR(0,2), ssb.stream, block, len)
	 == NULL;
}

static void
ssb_maybe_start (void)
{
  if (!ssb.started && ssb_queued (NULL) >= ssb.frag_out)
    ssb_pause (0);
}

static int
ssb_write (struct __unixlib_fd *fd, const void *data, int nbyte)
{
  const _kernel_oserror *err;
  const unsigned char *in = data;
  int fs = ssb_in_frame ();
  int left = nbyte, done = 0;
  int nonblock = (fd->fflag & O_NONBLOCK) != 0;
  int last_q = -1;
  clock_t since = clock ();

  if (ssb.conv == NULL && (ssb.conv = malloc (SSB_CONV_SIZE)) == NULL)
    return __set_errno (ENOMEM);
  if (!ssb.handle && (err = ssb_open_stream ()) != NULL)
    return __ul_seterr (err, EOPSYS);

  /* Finish an input frame left over from the last write.  */
  if (ssb.carry_len)
    {
      int need = fs - ssb.carry_len;
      if (left < need)
	{
	  memcpy (ssb.carry + ssb.carry_len, in, left);
	  ssb.carry_len += left;
	  return nbyte;
	}
      memcpy (ssb.carry + ssb.carry_len, in, need);
      ssb_convert (ssb.carry, (short *) ssb.conv, 1);
      while (!ssb_add (ssb.conv, 4))
	if (nonblock || !ssb_wait (&last_q, &since))
	  break;		/* drop one frame rather than hang */
      ssb.carry_len = 0;
      in += need; left -= need; done += need;
    }

  while (left >= fs)
    {
      int q = ssb_queued (NULL), space, frames, out;
      if (q < 0)
	return done ? done : __set_errno (EIO);
      space = ssb.cap_out - q;
      frames = left / fs;
      if (frames > SSB_CONV_SIZE / 4)
	frames = SSB_CONV_SIZE / 4;
      out = frames * 4;
      if (space < out)
	{
	  /* Full.  Wait for room for a whole block (or what's left), so we
	     don't trickle tiny blocks into the stream.  */
	  if ((space >= ssb.frag_out || space >= out / 2) && space >= 4)
	    out = space & ~3, frames = out / 4;
	  else
	    {
	      if (!ssb.started)
		ssb_pause (0);	/* queue full: must play now */
	      if (nonblock)
		break;
	      if (!ssb_wait (&last_q, &since))
		{
		  if (done)
		    break;
		  return __set_errno (EIO);
		}
	      continue;
	    }
	}
      ssb_convert (in, (short *) ssb.conv, frames);
      if (!ssb_add (ssb.conv, out))
	{
	  if (nonblock)
	    break;
	  if (!ssb_wait (&last_q, &since))
	    return done ? done : __set_errno (EIO);
	  continue;
	}
      in += frames * fs; left -= frames * fs; done += frames * fs;
      ssb_maybe_start ();
    }

  /* Keep an incomplete trailing frame for next time.  */
  if (left > 0 && left < fs)
    {
      memcpy (ssb.carry, in, left);
      ssb.carry_len = left;
      done += left;
    }

  if (done == 0 && nbyte > 0)
    return __set_errno (EAGAIN);
  return done;
}

/* Start playing and wait until everything queued has played.  */
static void
ssb_drain (void)
{
  int last_q = -1;
  clock_t since = clock ();
  if (!ssb.handle)
    return;
  if (!ssb.started)
    ssb_pause (0);
  while (ssb_queued (NULL) > 0 && ssb_wait (&last_q, &since))
    ;
}

static int
ssb_ioctl (struct __unixlib_fd *fd, unsigned long full_request, void *arg)
{
  int *iarg = arg;
  unsigned long request = full_request & 0xffff;

  switch (request)
    {
    case SNDCTL_DSP_RESET & 0xffff:	/* also SNDCTL_DSP_HALT */
      ssb_close_stream ();		/* drops what's queued */
      return 0;

    case SNDCTL_DSP_SYNC & 0xffff:
      ssb_drain ();
      return 0;

    case SNDCTL_DSP_POST & 0xffff:
      if (ssb.handle && !ssb.started)
	ssb_pause (0);
      return 0;
    }

  if (!arg)
    return __set_errno (EINVAL);

  switch (request)
    {
    case SNDCTL_DSP_SPEED & 0xffff:
      {
	int rate = *iarg;
	if (rate < 4000)
	  rate = 4000;
	if (rate > 96000)
	  rate = 96000;
	ssb.rate = rate;
	if (ssb.handle)
	  _swix (XSharedSoundBuffer_SampleRate, _INR(0,1), ssb.handle,
		 rate * 1024);
	*iarg = rate;
	return 0;
      }

    case SNDCTL_DSP_SETFMT & 0xffff:
      if (*iarg != AFMT_QUERY)
	{
	  ssb.format = (*iarg & SSB_FORMATS) && !(*iarg & (*iarg - 1))
		       ? *iarg : AFMT_S16_LE;
	  ssb.carry_len = 0;
	}
      *iarg = ssb.format;
      return 0;

    case SNDCTL_DSP_STEREO & 0xffff:
      ssb.channels = *iarg ? 2 : 1;
      ssb.carry_len = 0;
      *iarg = ssb.channels - 1;
      return 0;

    case SNDCTL_DSP_CHANNELS & 0xffff:
      if (*iarg != 0)
	{
	  ssb.channels = (*iarg == 1) ? 1 : 2;
	  ssb.carry_len = 0;
	}
      *iarg = ssb.channels;
      return 0;

    case SNDCTL_DSP_GETFMTS & 0xffff:
      *iarg = SSB_FORMATS;
      return 0;

    case SNDCTL_DSP_GETBLKSIZE & 0xffff:
      *iarg = ssb_to_client (ssb.frag_out);
      return 0;

    case SNDCTL_DSP_SETFRAGMENT & 0xffff:
      {
	int frags = (*iarg >> 16) & 0x7fff;
	int shift = *iarg & 0xffff, size;
	int fs = ssb_in_frame ();
	int min_cap = ssb.rate * 4 / 50;	/* 20 ms */
	int max_cap = ssb.rate * 4 * 2;		/* 2 s */

	/* 2026: check the exponent before shifting (1 << 40 is undefined),
	   and keep at least two fragments within the 2 s cap: a fragment
	   bigger than the cap made GETOSPACE report 0 fragments for ever.  */
	if (shift > 16)
	  shift = 16;
	if (shift < 7)
	  shift = 7;
	size = 1 << shift;
	/* Program bytes -> output bytes, whole frames.  */
	ssb.frag_out = (size / fs) * 4;
	if (ssb.frag_out > ((max_cap / 2) & ~3))
	  ssb.frag_out = (max_cap / 2) & ~3;
	if (ssb.frag_out < 64)
	  ssb.frag_out = 64;
	if (frags == 0 || frags == 0x7fff)
	  frags = SSB_NFRAGS_DEFAULT;
	if (frags < 2)
	  frags = 2;
	ssb.cap_out = ssb.frag_out * frags;
	if (ssb.cap_out < min_cap)
	  ssb.cap_out = min_cap;
	if (ssb.cap_out > max_cap)
	  ssb.cap_out = max_cap;
	if (ssb.handle)
	  _swix (XStreamManager_SetBuffer, _INR(0,1), ssb.stream,
		 ssb.cap_out * 2 + SSB_CONV_SIZE * 2);
	return 0;
      }

    case SNDCTL_DSP_GETOSPACE & 0xffff:
      {
	struct audio_buf_info *info = arg;
	int q = ssb_queued (NULL), space;
	if (q < 0)
	  q = 0;
	space = ssb.cap_out - q;
	if (space < 0)
	  space = 0;
	info->fragsize = ssb_to_client (ssb.frag_out);
	info->fragstotal = ssb.cap_out / ssb.frag_out;
	info->fragments = space / ssb.frag_out;
	info->bytes = ssb_to_client (space);
	return 0;
      }

    case SNDCTL_DSP_GETODELAY & 0xffff:
      {
	int q = ssb_queued (NULL);
	*iarg = q > 0 ? ssb_to_client (q) : 0;
	return 0;
      }

    case SNDCTL_DSP_GETOPTR & 0xffff:
      {
	struct count_info *info = arg;
	unsigned int played = 0;
	ssb_queued (&played);
	info->bytes = ssb_to_client ((int) (played - ssb.played_base));
	info->blocks = (int) ((played - ssb.last_frag) / (unsigned) ssb.frag_out);
	ssb.last_frag += (unsigned) info->blocks * ssb.frag_out;
	info->ptr = 0;
	return 0;
      }

    case SNDCTL_DSP_GETCAPS & 0xffff:
      *iarg = DSP_CAP_REALTIME;
      return 0;

    case SNDCTL_DSP_GETTRIGGER & 0xffff:	/* and SETTRIGGER */
      /* GETTRIGGER in either encoding (OSS_READ).  The second one has the
	 same value as SETTRIGGER in the first; writing PCM_ENABLE_OUTPUT
	 back to a SETTRIGGER caller is harmless (it is ignored anyway).  */
      if (OSS_IS_READ (full_request, 16))
	*iarg = PCM_ENABLE_OUTPUT;
      return 0;
    }

  return __set_errno (EINVAL);
}


/* ---- The /dev/dsp device: pick a back end when it's opened.  ---------- */

void __dsp_exit (void);

/* Called from _exit() in every program: only undo what this one did.
   2026: a fork()/vfork() child inherits (or shares) the parent's stream
   handle and DigitalRenderer state; its exit must not close them.  */
void
__dsp_exit (void)
{
  if (dsp_owner_pid != getpid ())
    return;
  ssb_close_stream ();
  dr_exit ();
  /* The process is ending: the descriptors are closed after this.  Not if
     another process (the parent of a vfork child that played on the
     parent's descriptor) opened them: they are still the parent's.  */
  if (dsp_open_pid == getpid ())
    {
      dsp_opens = 0;
      dsp_backend = BACKEND_NONE;
    }
}

void *
__dspopen (struct __unixlib_fd *fd, const char *file, int mode)
{
  const char *want = getenv ("UnixLib$DSP");
  int force_dr = want && (strcasecmp (want, "DigitalRenderer") == 0
			  || strcasecmp (want, "DRender") == 0);
  int force_ssb = want && strcasecmp (want, "SharedSound") == 0;
  void *ret;

  if (dsp_opens > 0 && dsp_backend != BACKEND_NONE)
    {
      dsp_opens++;
      return (void *) 1;
    }
  ssb_close_stream ();
  if (!force_dr && ssb_available ())
    {
      dsp_backend = BACKEND_SSB;
      ssb_set_defaults ();
      dsp_opens = 1;
      dsp_open_pid = getpid ();
      return (void *) 1;
    }
  if (force_ssb)
    {
      __set_errno (ENODEV);
      return (void *) -1;
    }
  dsp_backend = BACKEND_DR;
  ret = dr_open (fd, file, mode);
  if (ret != (void *) -1)
    {
      dsp_opens = 1;
      dsp_open_pid = getpid ();
    }
  return ret;
}

int
__dspclose (struct __unixlib_fd *fd)
{
  if (dsp_opens > 1)
    {
      dsp_opens--;		/* another descriptor still uses it */
      return 0;
    }
  dsp_opens = 0;
  if (dsp_backend == BACKEND_SSB)
    {
      /* Like OSS: let what was written finish playing.  */
      if (!(fd->fflag & O_NONBLOCK))
	ssb_drain ();
      ssb_close_stream ();
      dsp_backend = BACKEND_NONE;
      return 0;
    }
  dsp_backend = BACKEND_NONE;
  return dr_close (fd);
}

int
__dspwrite (struct __unixlib_fd *fd, const void *data, int nbyte)
{
  if (dsp_backend == BACKEND_SSB)
    return ssb_write (fd, data, nbyte);
  return dr_write (fd, data, nbyte);
}

int
__dspioctl (struct __unixlib_fd *fd, unsigned long request, void *arg)
{
  /* 2026: requests are matched on their low 16 bits below, but the
     read-only SOUND_PCM_READ_* requests have the same low bits as the
     setting ones (READ_RATE = SPEED, READ_BITS = SETFMT, READ_CHANNELS =
     CHANNELS): READ_RATE with 0 used to set the rate to 4000 Hz.  Answer
     them here from the full request, in either encoding (see
     OSS_READ).  */
  if (OSS_IS_READ (request, 2) || OSS_IS_READ (request, 5)
      || OSS_IS_READ (request, 6))
    {
      int ssbe = dsp_backend == BACKEND_SSB;
      int fmt = ssbe ? ssb.format : dr_format;
      if (!arg)
	return __set_errno (EINVAL);
      if (OSS_IS_READ (request, 2))
	*(int *) arg = ssbe ? ssb.rate : dr_frequency;
      else if (OSS_IS_READ (request, 6))
	*(int *) arg = ssbe ? ssb.channels : dr_channels;
      else
	*(int *) arg = (fmt == AFMT_S16_LE || fmt == AFMT_S16_BE) ? 16 : 8;
      return 0;
    }
  if (dsp_backend == BACKEND_SSB)
    return ssb_ioctl (fd, request, arg);
  return dr_ioctl (fd, request, arg);
}
