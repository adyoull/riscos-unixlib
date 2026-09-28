# Changelog

Versions are UnixLib version numbers, continuing from GCCSDK's UnixLib 5.0
(the base, see README); git tags are `vX.Y.Z`. The first releases of this
repo were numbered 0.1.x: 0.1.0, and 0.1.1-rc1, the pre-release of 5.0.1.
How to release: docs/MAINTAINING.md.

## Unreleased

- **Files over 2GB** (up to 4GB-1, the RISC OS limit) for programs built
  with `-D_FILE_OFFSET_BITS=64` or using the `*64` calls: `struct stat64`
  has a 64-bit `st_size` (new functions `__unixlib_stat64`/`fstat64`/
  `lstat64`; the old `stat64` symbols keep the old layout for libraries
  built with older headers, such as libstdc++), `lseek64` and the stdio
  64-bit calls reach 4GB-1, new `truncate64`/`ftruncate64` and `mmap64`,
  and `fsetpos64` works (it used the pointer instead of the position).
  Nothing changes for programs built without `_FILE_OFFSET_BITS=64`.
  docs/LARGE-FILES.md. Checks: `tests/abi/check.sh` (layouts and
  symbols, in `make check`), Pi test `LargeFile`. **Not yet run on RISC
  OS.**

- Docs: where to get SharedSoundBuffer and StreamManager (README,
  docs/SOUND.md, the test ReadMe): `ssb.zip` from Andrew Sellors' RDPClient
  page, and John Duffell's own site on the Internet Archive for details.

## 5.0.1 (2026-09-28, tag v5.0.1)

Release files also include `PThreadTicker-0.01.zip` for RISC OS users.

**Tested on RISC OS:** Warzone 2100 (2.3.9-2test1) linked with 0.1.1-rc1,
with the PThreadTicker module, on a Pi: 17 minutes of play with other
tasks running, no crashes; the TickerStats line confirmed the cause
(docs/THREAD-TICKER.md). Not yet run on RISC OS: a program without the
module (the RMA copy; the same code Warzone riscos16/17 ran), refusing
`*RMKill` while in use, and the programs in UnixLibTests.zip.

### Since 0.1.1-rc1

- Numbered as UnixLib 5.0.1 (`configure.ac`, `doc/UnixLib/Help`).

- The `UnixLib$TickerStats` line gives the program's leaf name (it was
  the full path cut to 47 characters) and a Wimp version of 0 when there
  is no task (it printed whatever was in R1).

## 0.1.1-rc1 (2026-09-27, pre-release, tag v0.1.1-rc1)

Pre-release of 5.0.1, not tested on RISC OS; everything below is in 5.0.1.

### Library

- **Thread ticker can run from a module, PThreadTicker** (new
  `module/pthticker.s`, `incl-local/internal/ticker.s`, new
  `pthread/ticker.c`), from Warzone 2100's request to find the root cause
  and stop running code out of an RMA data block. The ticker handler and
  the Wimp filters are one set of routines, assembled into the module and
  into UnixLib. When the module is loaded UnixLib runs its routines (found
  with OS_Module 18 and an interface table; no SWIs, nothing to allocate);
  otherwise it runs its own copy from the RMA block, as below. The module
  refuses to be killed while programs use it. The filters now follow the
  task handle: read afresh when the ticker starts and every ~1.3 s while it
  runs (threads created before `Wimp_Initialise`, like SDL's, left the
  ticker unfiltered for good), and removed with the handle they were
  registered for. SharedUnixLibrary is unchanged (a first version put the
  routines in a SUL "1.17"; withdrawn, SUL belongs to GCCSDK). Details:
  docs/THREAD-TICKER.md. **Needs the Pi tests** `Ticker`, `TickerEarly`,
  `TickerStartTask`, with and without the module.
- **`UnixLib$TickerStats`**: set it to a file name and every UnixLib
  program appends a line of thread ticker counters to it at exit (ticks,
  ticks that found another task paged in, filter calls, task handles), to
  find out on a real machine why the ticker ran in other tasks.
- **Thread ticker handler runs from RMA** (`pthread/_context.s`,
  `pthinit.c`, `incl-local/pthread.h`, `asm_dec.s`), from the Warzone 2100
  port (riscos14). A program with more than one thread installs an
  OS_CallEvery handler to switch threads. It lived in the program's memory,
  so when the ticker fired while another task was paged in, *that* task
  crashed ("Internal error: abort on instruction fetch" in Organizer, while
  Warzone was running). The handler only ever used its RMA block and SWIs,
  so it is now copied there at start-up and runs from there; in another
  task it sees that task's upcall handler and returns. Compile-time checks
  keep the C and assembler descriptions of the RMA block in step.
  **Needs the Pi tests `Ticker` / `TickerEarly`.** Every threaded program
  (OpenTTD, anything using SDL threads) should be relinked with it.
- **Mixed old and new objects can't happen silently any more.** The ticker
  change grew the pthread RMA block in `asm_dec.s`, and UnixLib's makefile
  didn't rebuild assembler files when it changed: Warzone's incremental
  build kept an old `_syslib.o` claiming 120 bytes, the handler was copied
  past the end, and the Pi hung hard. Now assembler files depend on the
  shared assembler definitions, `__pthread_prog_init` stops with a clear
  error if the claimed size doesn't match, and `tools/check-lib.sh` checks
  a library or program (run by every build here). Warzone riscos16/17, built
  consistently, run on a Pi 4 with the ticker active and no aborts in other
  tasks.

- **`sched_get_priority_min` / `sched_get_priority_max`** added
  (`sched/sched_prio.c`, `<sched.h>`), from riscos-mesa: OpenAL Soft found
  `pthread_setschedparam`, enabled its priority code and failed to link.
  Every known policy has the single priority 0 (the scheduler ignores
  priorities).
- **`pthread_setschedparam`** refused every policy (a `||` that should have
  been `&&`). Now SCHED_OTHER/0 is accepted, SCHED_FIFO/RR/SPORADIC give
  ENOTSUP, anything else EINVAL. New threads start as SCHED_OTHER/0; before,
  `pthread_getschedparam` returned uninitialised memory. Pi test: `Sched`.

### Repo (nothing changes in the library)

- `tools/make-release.sh` / `make release TAG=...`: clean build, all
  checks, and the release files (library, patches, tests, the module zip
  with a ReadMe and licence, SHA256SUMS) in `out/release/<tag>/`.
- `tools/check-lib.sh` / `make check-lib FILES=...` (see above). The
  pthread RMA block is now 472 bytes.
- Tests: `tests/host/ticker` (C, fake SWIs) and `tests/emu/ticker_test.py`
  (the built PThreadTicker module and UnixLib's ticker code in the Unicorn
  ARM emulator; skipped without a build or `pip install unicorn`). Pi test
  `TickerStartTask`; the Ticker tests set `UnixLib$TickerStats`;
  UnixLibTests.zip carries the module and `LoadTicker`.

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
