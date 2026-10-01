# Sound in UnixLib: /dev/dsp and /dev/midi

## /dev/dsp (OSS audio out)

Open `/dev/dsp`, set the format with the usual OSS ioctls, and `write()`
samples. SDL 1.2's and SDL 2's `dsp` drivers, ffplay/mplayer builds that use
OSS, and simple players all work this way.

**Output.** When SharedSound 1.07+, StreamManager 0.03+ and
SharedSoundBuffer 0.07+ are loaded, UnixLib plays through
SharedSoundBuffer: the sound is mixed with every other program's, any
sample rate is resampled, and quitting one program doesn't affect the
others. Otherwise it uses DigitalRenderer as before (one program at a
time). SharedSound is part of RISC OS; SharedSoundBuffer and StreamManager
are John Duffell's freeware, which can't be bundled. Download `ssb.zip` from
Andrew Sellors' RDPClient page,
<https://orac.co.uk/software/rdpclient/rdpclient.html>, and merge its
`!System` into yours. John Duffell's own site (now on the Internet Archive)
has more details:
<https://web.archive.org/web/20110920080106/http://www.duffell.riscos.me.uk/>.
Load them with:

    RMEnsure SharedSound 1.07 RMLoad System:Modules.SSound
    RMEnsure StreamManager 0.03 RMLoad System:Modules.StreamMan
    RMEnsure SharedSoundBuffer 0.07 RMLoad System:Modules.SSBuffer

`UnixLib$DSP` chooses: `DigitalRenderer` (or `DRender`) always uses the old
path; `SharedSound` never falls back (`open` fails with `ENODEV` if the
modules are missing).

**Formats (SharedSoundBuffer path).** 16-bit signed LE and BE, 8-bit signed
and unsigned, µ-law; mono or stereo; 4000–96000 Hz. UnixLib converts to
16-bit stereo. Anything else asked for with `SNDCTL_DSP_SETFMT` gets 16-bit
LE, and the ioctl says so, as OSS does.

**Buffering.** 8 fragments of 1024 frames by default (about 190 ms at
44.1 kHz). `SNDCTL_DSP_SETFRAGMENT` sets it (20 ms – 2 s). Playing starts
once a fragment is queued, or on `SNDCTL_DSP_POST`/`SYNC`/`close()`.
`GETOSPACE`, `GETODELAY` and `GETOPTR` report the real queue, so programs can
keep sound and pictures in step.

**Writes.** Blocking writes wait for room (spinning with `pthread_yield`, so
other threads keep running). `O_NONBLOCK` writes take what fits and return
`EAGAIN` when full. If the output stops moving for 2 s, the write returns
short instead of hanging.

**Closing.** `close()` waits until what was written has played (not with
`O_NONBLOCK`). At exit, a stream still open is closed at once.
`SNDCTL_DSP_RESET` drops what's queued.

**The exit bug.** Every UnixLib program used to stop DigitalRenderer when it
quit, even if it never made a sound, so quitting any UnixLib program cut off
another one's sound. Now a program only stops what it started:

- A program that never played leaves DigitalRenderer alone.
- If another program took DigitalRenderer over, the first one's exit
  leaves the new owner alone. The owner's pid is kept in the system
  variable `UnixLib$DSPOwner`; a program built with an older UnixLib
  doesn't set it, so this only works when both programs use this one.
- A fork/vfork child's exit leaves the parent's stream or session alone.

**More than one open.** The device is per program: a second `open` of
`/dev/dsp` shares the first one's stream and settings (changing a setting
on one changes it for both), and only the last `close` drains and closes
the stream.

**Reading settings.** `SOUND_PCM_READ_RATE`, `SOUND_PCM_READ_CHANNELS` and
`SOUND_PCM_READ_BITS` report the current settings without changing them.

## /dev/midi (raw MIDI out)

Write MIDI bytes (with running status and SysEx) to `/dev/midi` (or
`/dev/midi0`, `/dev/midi00`) and they play at once. They go to the MIDISynth
module, a General MIDI software synth shared by all programs (planned; SWIs
in [MIDISYNTH-MODULE.md](MIDISYNTH-MODULE.md)), or else to the RISC OS MIDI
module (`MIDI_TxByte`: external MIDI or USB MIDI). `UnixLib$MIDI` =
`MIDISynth` or `MIDI` allows only that one. Nothing loaded: `ENODEV`.

`SNDCTL_SEQ_RESET` / `SNDCTL_SEQ_PANIC` turn all notes off. Notes are also
turned off when the program closes `/dev/midi` or exits.

There's no `/dev/sequencer` (timed events) and no MIDI in.

## Tests

- Host: `tests/host/dsp/run.sh`, `tests/host/midi/run.sh` (fake modules).
- RISC OS: `tests/riscos/build.sh` makes `UnixLibTests.zip` (see its ReadMe).
