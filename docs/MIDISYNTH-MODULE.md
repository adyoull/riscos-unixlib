# MIDISynth module: the SWIs UnixLib's /dev/midi expects

**Status:** proposal. UnixLib's `/dev/midi` (`libunixlib/sound/midi.c`) is
written against it and tested with a fake module. The module itself doesn't
exist yet; it belongs to the riscos-midisynth project.

## Why a module

UnixLib is linked into every program, so it can't carry a synthesiser and a
6 MB SoundFont itself. One relocatable module does the synthesis for every
program on the machine: one SoundFont in memory, one SharedSoundBuffer
stream, and any language can use it (C through `/dev/midi`, BASIC through
the SWIs).

## SWIs

UnixLib finds them with `OS_SWINumberFromString`, so the chunk number can be
anything (get one allocated by ROOL before release). Names are what matter.
All SWIs are called in user mode, and UnixLib calls them with the X bit set.

### MIDISynth_Open
- In: R0 = flags (0), R1 → client name (null-terminated, e.g. the program
  name; for display only).
- Out: R0 = client handle (non-zero).
- Starts the synthesiser (SoundFont load, SharedSoundBuffer stream) if this
  is the first client. May return an error ("No SoundFont", "Sound modules
  not loaded", "Not enough memory"); UnixLib passes it to the program as
  `EOPSYS` and `open()` fails.

### MIDISynth_Write
- In: R0 = handle, R1 → bytes, R2 = number of bytes.
- Out: R0 = number of bytes taken (0 … R2).
- Raw MIDI byte stream, played as soon as possible (no time stamps).
- The module keeps a parser **per client**: running status, SysEx
  (`F0 … F7`, may be ignored but must be skipped correctly), and a message
  split across two calls. System real-time bytes (`F8`–`FF`) may appear
  anywhere and don't break running status.
- Taking fewer than R2 bytes means "queue full, try later"; UnixLib then
  returns a short write. A module that takes everything at once is fine.

### MIDISynth_Reset
- In: R0 = handle.
- All notes off, sustain off and reset all controllers on all 16 channels,
  pitch bend centred, programs back to 0. (UnixLib uses it for
  `SNDCTL_SEQ_RESET` / `SNDCTL_SEQ_PANIC`.) Optional: UnixLib skips it if the
  SWI is missing.

### MIDISynth_Close
- In: R0 = handle.
- Turns off any notes the client left sounding (at least all notes off on
  the channels it used). Stops the synthesiser when the last client
  closes (or keeps it warm for a few seconds, the module's choice).
- UnixLib calls it on `close()`, and from `_exit()` if the program never
  closed `/dev/midi` (including after a crash), so notes don't hang.
- The module should also tidy up clients whose task has gone (for BASIC
  programs that stop without closing).

## Sharing

All clients drive the same 16 channels, as with one hardware synth on a MIDI
cable. That's normal for GM; a later version could give each client its own
channel set (a flag in `MIDISynth_Open`).

## Suggested extras (not used by UnixLib)

- `MIDISynth_SoundFont` (R0 → path) to load another SoundFont.
- `MIDISynth_Volume` (R0 = 0–256).
- `MIDISynth_PlayFile` (R0 → .mid path) for BASIC programs.
- `*MIDISynthInfo` showing the SoundFont, clients and voices in use.

## Without the module

`/dev/midi` falls back to the RISC OS MIDI module (`MIDI_TxByte`, external
MIDI hardware or USB MIDI). With neither, `open("/dev/midi")` fails with
`ENODEV`. `UnixLib$MIDI` set to `MIDISynth` or `MIDI` allows only that one.
