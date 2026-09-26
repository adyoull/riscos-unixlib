# Changelog

Versions are git tags `vX.Y.Z`. How to release: docs/MAINTAINING.md.

## Unreleased (0.1.1)

For people working on the repo; nothing changes in the library.

- `build/fetch-sources.sh` fetches and checks GCCSDK and the GCC source
  (pins in `build/sources.conf`); `build-unixlib.sh` uses them by default.
- `tools/elf2aif`: the large-image elf2aif, copied from riscos-openttd.
- `make check` (`tests/check.sh`): host tests, patches match the history and
  apply to GCCSDK, scripts parse. Runs on GitHub Actions. `Makefile` with
  shortcuts. `tools/make-patches.sh` generates `patches/`.
- Host test fakes shared in `tests/host/fake`; `tests/README.md`.
- `docs/MAINTAINING.md`: layout, how to change and release, traps.
- The RISC OS test programs are AIF files (the ELF ones had problems on the
  Pi).

## 0.1.0 (2026-09-26, tag v0.1.0 = `22511f2`)

The first version: everything below, in three parts.

### Fixes from the Warzone 2100 port

Handoff from Warzone 2100 (riscos7), found on the Pi.

- **fsync on read-only files** (`unix/sync.c`): returned -1/EBADF; now 0, as
  glibc does. PhysFS calls fsync before close and gave up on failure, so
  read-only files were never closed ("Error closing config: Bad file
  descriptor", then "This file is already open" on every save).
- **fdatasync()** added (same as fsync).
- **Joining a thread at exit** (`stdlib/atexit.c`): `__cxa_finalize` blocked
  thread switching for the whole walk, so an atexit handler or C++
  destructor that waited for a thread (SDL_WaitThread → pthread_join →
  pthread_yield) hit `__pthread_fatal_error`: "Fatal signal received:
  Aborted" on quit. Switching is now blocked only while the list is read.
- **Unaligned access** (EMT trap in Warzone's map loader): not a UnixLib bug;
  RISC OS faults unaligned loads. Documented in the README porting notes.
- Tests: `ExitJoin` and `FsyncRO` in `UnixLibTests.zip` (renamed from
  UnixLibSound.zip). **Not yet run on RISC OS.**

### Sound

Asked for on the ROOL forum: SharedSoundBuffer output for UnixLib's OSS
device (it makes a big difference to ffplay), and a fix for "every time a
program quits the sound gets reset, even if no audio is used".

- **Exit bug fixed** (`sound/dsp.c`): `_exit()` calls
  `__dsp_exit()` in every program, and it deactivated DigitalRenderer
  whether or not the program had played anything, cutting off whichever
  program was playing. Now only the program that activated DigitalRenderer
  deactivates it. Opening `/dev/dsp` and ioctls no longer stop or
  reconfigure another program's DigitalRenderer session either; settings
  apply when the program first writes.
- **Default format** is 16-bit as intended (the open passed 2 = A-law).
- **SharedSoundBuffer / StreamManager output** for `/dev/dsp` when the modules
  are loaded (SharedSound 1.07+, StreamManager 0.03+, SharedSoundBuffer
  0.07+). Mixes with other programs; any rate 4–96 kHz. Converts S16 LE/BE,
  U8, S8, µ-law, mono/stereo to S16LE stereo. OSS ioctls: SPEED, SETFMT,
  STEREO, CHANNELS, GETFMTS, GETBLKSIZE, SETFRAGMENT, GETOSPACE, GETODELAY,
  GETOPTR, SYNC, POST, RESET, GETCAPS, GET/SETTRIGGER. About 190 ms queued by
  default (the DigitalRenderer path queued about 5 s). Blocking and
  O_NONBLOCK writes. `close()` plays out what's queued. `UnixLib$DSP` =
  `DigitalRenderer` or `SharedSound` picks one.
- **`/dev/midi`** (new `sound/midi.c`, device `DEV_MIDI`): raw MIDI out to a
  MIDISynth module (proposed SWIs in `docs/MIDISYNTH-MODULE.md`) or the MIDI
  module (`MIDI_TxByte`). Notes are turned off on close and at exit.
- Host tests: `tests/host/dsp` (124 checks), `tests/host/midi` (20 checks).
  RISC OS test programs: `tests/riscos` (UnixLibTests.zip). **Not yet run on
  RISC OS.**

### The ports' changes, merged (the first commits)

Merges every UnixLib change made by the RISC OS ports so far. Base: GCCSDK
`64c6f81` (jhamby/riscos-gccsdk), imported unchanged.

#### From the OpenTTD port (riscos-openttd `patches/unixlib`, used since 14.1-riscos1)

- **Wide characters** (`wchar/wmissing.c`, `wchar/wctype.c`): real versions of
  the functions that were "Not implemented" + `abort()`: `wctype`,
  `wctype_l`, `iswctype`, `iswctype_l`, `iswupper`, `iswlower`, `iswblank`
  (and `_l`), `wcscoll`, `wcsxfrm` (and `_l`), `wcscasecmp`, `wcsncasecmp`
  (and `_l`), `wcstol`, `wcstoul`, `wcstoll`, `wcstoull`, `wcstod`,
  `wcstof`, `wcstold`, `swprintf` (no `%ls`), `wcsftime`, `putwc`, `getwc`,
  `ungetwc`. 8-bit/Latin-1, matching UnixLib's ctype tables. The `isw*`
  functions in `wctype.c` return 0 above 255 instead of reading past the
  tables. Fixes C++ programs dying at start with "wctype: Not implemented"
  (libstdc++ `std::locale`). Confirmed on the Pi by OpenTTD and Warzone 2100.
- **High resolution monotonic clock** (`time/clk_gettime.c`):
  `CLOCK_MONOTONIC` = monotonic centiseconds + HAL counter position within
  the centisecond. Checks the counter reloads once per centisecond, else
  falls back. Never goes backwards. Removed a regular stutter in OpenTTD.
- **nanosleep** (`signal/sleep.c`): whole centiseconds via the old sleep, the
  rest waited out on the new clock with `pthread_yield()`. `EINVAL` is now
  returned (it used to set errno and carry on). `rem` is set when
  interrupted.
- **malloc** (`stdlib/alloc.c`): `DEFAULT_MMAP_MAX 0` on EABI. Large blocks
  come from the heap dynamic area instead of leaking `mmap#N` areas.

#### From the SDL2 / riscos-mesa work

No UnixLib source changes: the SDL overlay works around UnixLib (its own
Wimp_PollIdle delay, its own sound driver). The UnixLib problems it found are
recorded in [docs/TODO.md](docs/TODO.md) for a later release.

#### Repo

- `patches/unixlib-riscos.diff`: the whole change set for a GCCSDK checkout.
- `build/build-unixlib.sh`: rebuild just `libunixlib.a` for an installed
  toolchain (reproduces the Warzone toolchain's library exactly).
