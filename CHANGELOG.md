# Changelog

Versions are UnixLib version numbers, continuing from GCCSDK's UnixLib 5.0
(the base, see README); git tags are `vX.Y.Z`. The first releases of this
repo were numbered 0.1.x: 0.1.0, and 0.1.1-rc1, the pre-release of 5.0.1.
How to release: docs/MAINTAINING.md.

## Unreleased

- **`nanosleep`, `sleep`, `usleep`:** long sleeps are made in chunks
  `ualarm` can express (above about 71 minutes it overflowed and the sleep
  ended early, after which `nanosleep` busy-waited for the rest);
  `nanosleep` sleeps again if a sleep ends early without a signal; waiting
  no longer aborts the program when called with thread switching held off;
  `usleep` with 1000000 or more now fails with `EINVAL` instead of setting
  `errno` and sleeping anyway.

- **`CLOCK_MONOTONIC`:** the last value returned (64 bits) is read and
  updated with thread switching held off, so another thread can't see half
  an update and make the clock jump ahead and stick.

- **`wcstol` and the rest of `wcsto*`:** numbers longer than 127
  characters are converted in full (they were cut short).

- **`iswalpha_l` and the other `isw*_l`/`tow*_l` functions:** characters
  outside 0-255 return 0 (or are returned unchanged) instead of reading
  past the end of the ctype tables, as the non-locale versions already did.

- **`read()` into a stack buffer:** a buffer in another thread's stack is
  mapped in before the read even when that stack lies below the calling
  thread's stack pointer (it was skipped). This costs one SWI per `read()`
  of a RISC OS file for buffers that were skipped before.

- **`/dev/dsp`:** `SNDCTL_DSP_SETFRAGMENT` checks the size before using it
  (a size exponent above 31 was undefined) and keeps fragments to at most
  half the 2 s limit (a bigger one made `GETOSPACE` report 0 fragments for
  ever); `GETOPTR` after `SNDCTL_DSP_RESET` counts from zero instead of
  returning a huge block count; the 8 KB conversion buffer is allocated on
  first use instead of being in every program's static data.
- **`/dev/midi`:** when MIDISynth is full, a write that sent nothing no
  longer returns 0 (which write-all loops retry for ever): `O_NONBLOCK`
  writes fail with `EAGAIN`, blocking ones wait up to 2 s for room, then
  fail with `EIO`. Closing `/dev/midi` resets MIDI hardware only if the
  program sent something.

## 5.0.3.1 (2026-10-01, pre-release, tag v5.0.3.1-rc1)

Fixes for problems in 5.0.3 (and earlier) found by an independent review
of all the changes (2026-10-01). **Not yet run on RISC OS**, so released as
a pre-release; once the Pi tests pass, the same commit is tagged v5.0.3.1.
The library's exported symbols are the same as 5.0.3's apart from one
internal function (`__pthread_ticker_owner`).

- Numbered as UnixLib 5.0.3.1 (`configure.ac`, `doc/UnixLib/Help`).

- **Thread ticker: a fork/vfork child's exit no longer tears down the
  parent's ticker.** `_exit()` in the child (e.g. after a failed `exec`)
  freed the parent's pthread RMA block and detached it from PThreadTicker;
  the parent then restarted its ticker on the freed block, which now holds
  running code and counters. Only the process that set the ticker up
  (`__pthread_ticker_owner`) frees it now.
- **Sound: a fork/vfork child's exit no longer closes the parent's
  SharedSoundBuffer stream or MIDISynth connection, or stops its
  DigitalRenderer session.**
- **`/dev/dsp` opened a second time no longer breaks the first
  descriptor** (it closed its stream, reset its settings, and closing it
  switched the first to DigitalRenderer; SDL's device probing does this).
  Further opens share the first one's stream and settings.
- **`SOUND_PCM_READ_RATE` no longer sets the rate to 4000 Hz**
  (`READ_CHANNELS` and `READ_BITS` had the same problem): they are told
  apart from SPEED/CHANNELS/SETFMT by the full request now, in both of
  `<sys/soundcard.h>`'s encodings (with or without `<sys/ioctl.h>` first).
  `GETTRIGGER` likewise.
- **DigitalRenderer takeover:** a program whose DigitalRenderer session
  another program took over no longer stops that program's sound when it
  exits, and doesn't take it back on every write either (it streams into
  the new owner's session, as before). The owner's pid is kept in the
  system variable `UnixLib$DSPOwner`; programs built with older UnixLibs
  don't set it, and can't be told apart.
- **`swprintf` and `wcsftime` refuse formats with characters above 0xFF**
  (EILSEQ / 0). Characters such as U+FF25 were narrowed to `%` and started
  conversions with no argument behind them.
- **`glob()` with `GLOB_ALTDIRFUNC` in programs built with
  `_FILE_OFFSET_BITS=64`** no longer overruns a stack buffer by 8 bytes
  (new in 5.0.2: the program's `gl_stat` fills the larger struct stat).
- Docs: README, `doc/UnixLib/Help`, MODIFICATIONS, the PThreadTicker
  ReadMe and MAINTAINING say that riscos-unixlib is an **unofficial fork**
  of GCCSDK's UnixLib, not a GCCSDK release; `configure.ac`'s bug-report
  address is this repo's issues page.
- Tests: host tests for the sound, wide-character and ticker fixes (dsp
  156 checks, midi 24, ticker 38, new `tests/host/wchar` 8); the previous
  code fails them. The sound tests now use UnixLib's `<sys/soundcard.h>`
  (they used the PC's). Not covered: `__pthread_prog_fini` itself (only
  the ticker functions it calls are), and `glob()` (checked by its stack
  frame size only).
- The fixes were reviewed again (Opus and Sonnet). That review found a
  regression in the first version (`SNDCTL_DSP_PROFILE` special case broke
  `GETODELAY` for programs including `<sys/ioctl.h>` first), two
  DigitalRenderer programs fighting over it, and a vfork case; fixed.

## 5.0.3 (2026-09-30, tag v5.0.3)

Fixes found by Warzone 2100 (on another user's Pi 4) and riscos-plex. The
library's exported symbols are the same as 5.0.2's, and only 6 objects'
machine code changed. Checked with the emulator test (`tests/emu/swi_test.py`,
which fails on 5.0.2) and `make check`; not yet run on the machine where
Warzone crashed.

- Numbered as UnixLib 5.0.3 (`configure.ac`, `doc/UnixLib/Help`).

- **Build paths no longer end up in `libunixlib.a`** (riscos-plex). The
  debug information in the 5.0.2 library recorded the directory it was
  built in (and the toolchain's header directory under the builder's home
  directory), and programs linked with it and shipped as ELF, or kept as
  `_g` debug builds, carried them too. `build/build-unixlib.sh` now maps
  them to `/riscos-unixlib` and `/gccsdk-env` (`-fdebug-prefix-map`) and
  stops if the library or the module still contains a path from the
  build machine; `make release` checks every release file the same way.
  The machine code is identical; only the debug information changes.
  AIF programs (`!RunImage`) never had them: elf2aif leaves out debug
  sections.

- **`read()` into a stack buffer whose pages hadn't been used yet could
  kill the program** ("Fatal signal received: EMT trap"; Warzone 2100 with
  fontconfig on one Pi 4). ARMEABISupport maps a stack's pages in when
  they're first touched, but a SWI (OS_GBPB) writing to such a page
  aborts in SVC mode. `read()` on a RISC OS file now reads one byte of
  each page of the part of the buffer that is in a stack first
  (`ARMEABISupport_StackOp` 2 and 3 say whether it is, and the stack's
  bounds). Buffers outside a stack aren't touched; nothing is written.
  GCCSDK's fontconfig port had worked around the same thing.

- **`ctime`, `ctime_r`, `asctime` and `asctime_r` returned a bad
  pointer** (Warzone 2100: crash in `strlen` at start-up on one Pi 4).
  The inline wrapper for Territory_ConvertDateAndTime in `time/stdtime.c`
  didn't say the SWI changes R2 ("bytes left in the buffer"), so GCC
  returned that instead of the buffer: 39 (&27) with the static buffer,
  1 with a caller's 26-byte buffer. Fixed, and every inline SWI wrapper
  was checked against the PRM:
  - `OS_GBPB` read/write (`internal/os.h`): R2 is an output (address after
    the last byte);
  - `OS_GetEnv` (SharedCLibrary build, `stdio/err.c`): R1 and R2 are
    outputs;
  - SharedCLibrary `gethostbyname`: R0 and R1 are outputs, and it returns
    NULL on failure (it returned whatever was in R1);
  - wrappers that pass pointers (file names, buffers, the Socket SWIs)
    now say the SWI reads or writes memory ("memory" clobber).
  None changes an interface: the library exports exactly the same
  symbols, and only 6 objects' code changed (`stdtime.o`, `dev.o`, and
  register allocation in `rename.o`, `tty.o`, `vfork.o`, `symlink.o`).

- Docs: MODIFICATIONS.md lists every difference from GCCSDK UnixLib
  (file inventory, and for each change the problem, evidence, reasons,
  effect and how it was checked).

## 5.0.2 (2026-09-29, tag v5.0.2)

**Tested on RISC OS:** the `LargeFile` test on a Pi (SDFS): a 3GB file,
seeking, sizes and data past 2GB with the POSIX and stdio calls, EOVERFLOW
at 4GB and EFBIG at 5GB, all ok, PASS. The pre-release v5.0.2-rc1
(2026-09-28) had the same library.

### Since 5.0.2-rc1

- Pi tests: the programs `largefile`, `exitjoin` and `fsyncro` had the same
  names as their Obey files apart from case, which is the same name on RISC
  OS, so unzipping overwrote one with the other (the LargeFile test crashed
  with "undefined instruction at &8034"). Renamed `lfstest`, `ejtest` and
  `fsrotest`; `tests/riscos/build.sh` now stops on such clashes. Only
  UnixLibTests.zip changes, not the library.

### Since 5.0.1

- Numbered as UnixLib 5.0.2 (`configure.ac`, `doc/UnixLib/Help`).

- **Files over 2GB** (up to 4GB-1, the RISC OS limit) for programs built
  with `-D_FILE_OFFSET_BITS=64` or using the `*64` calls: `struct stat64`
  has a 64-bit `st_size` (new functions `__unixlib_stat64`/`fstat64`/
  `lstat64`; the old `stat64` symbols keep the old layout for libraries
  built with older headers, such as libstdc++), `lseek64` and the stdio
  64-bit calls reach 4GB-1, new `truncate64`/`ftruncate64` and `mmap64`,
  and `fsetpos64` works (it used the pointer instead of the position).
  Nothing changes for programs built without `_FILE_OFFSET_BITS=64`.
  docs/LARGE-FILES.md. Checks: `tests/abi/check.sh` (layouts and
  symbols, in `make check`), Pi test `LargeFile` (passed on a Pi).

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
