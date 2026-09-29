# Tests

## `check.sh` (run with `make check`)

Runs everything that doesn't need RISC OS or the cross compiler: the host
tests below, a check that `patches/` matches the git history, a dry run of
both patches against unchanged GCCSDK UnixLib, and a syntax check of the
scripts. GitHub Actions runs it on every push.

## `host/`: UnixLib code on a PC with a fake RISC OS

| Directory | Tests | Checks |
|---|---|---|
| `host/dsp` | `sound/dsp.c`: SharedSoundBuffer output (formats, partial frames, blocking/non-blocking, latency, fragments, reset, a stalled stream, exit) and the DigitalRenderer path, including the exit bug | 124 |
| `host/midi` | `sound/midi.c`: MIDISynth module and MIDI module paths, sharing, exit, env overrides | 20 |
| `host/ticker` | `pthread/ticker.c`: the PThreadTicker module's routines (found by name, attach/detach, a wrong magic ignored) or the RMA copy, the Wimp filters following the task handle (and removed with the handle they were registered with), threads before `Wimp_Initialise`, registration failures, the `UnixLib$TickerStats` line. Has its own `fake/` for the few UnixLib internals used | 34 |
| `host/fake` | The fake RISC OS, shared by both: `swis.h`/`kernel.h` (a variadic `_swix`), `internal/*.h` (the few UnixLib internals used), `DRender.h` (fake DigitalRenderer), `riscos.c`/`riscos.h` (SharedSoundBuffer and StreamManager playing in simulated time, `clock`, `pthread_yield`, `getenv`), `prelude.h` (renames those calls to the fakes) | |

Run one with `host/dsp/run.sh`, `host/midi/run.sh` or `host/ticker/run.sh`. The midi test has its
own fake SWIs in `test_midi.c`; it only uses the headers from `fake/`.

Before trusting a new check, break the code it guards and see it fail.

Read the "Traps" section of `docs/MAINTAINING.md` before editing the fakes
(header order in `prelude.h`, pointers below 4 GB).

## `emu/`: machine code in an ARM emulator

`emu/ticker_test.py` runs the thread ticker routines from a build (the
PThreadTicker module `build/work/build/pthticker` and UnixLib's
`_context.o`) in the Unicorn ARM emulator with faked SWIs: the module's
header and interface table, its workspace, attach/detach and refusing to
be killed while in use, the handler with our task and another task paged
in, the filters (registers and flags preserved), and UnixLib's copy run
from another address. 77 checks.

`emu/swi_test.py` links `emu/swi_stub.c` with the built `libunixlib.a` and
runs library functions that call SWIs, with the SWIs faked as RISC OS
behaves: `__standard_time` (`ctime`, `asctime`) must return its buffer
although Territory_ConvertDateAndTime changes R2, and `__fsread`
(`read()`) must touch every page of a stack buffer before OS_GBPB writes
to it, and leave other buffers alone. 20 checks; the library before these
fixes fails 6 of them (`ctime` returns &27, as in Warzone 2100's crash).

`make check` runs them when there's a build, the cross toolchain and the
Python `unicorn` module (`pip install unicorn`); otherwise it says SKIP.

## `abi/`: what compiled code depends on

`abi/check.sh` compiles small files with the repo's headers and checks
struct layouts in the default and large-file modes
(`expected-layout.txt`; the default mode must stay as UnixLib 5.0.1),
which symbols stat/seek/truncate/mmap calls go to (`expected-calls.txt`),
that the library still exports the old `stat64`/`fstat64`/`lstat64`, and
that a C++ program links with the toolchain's libstdc++. `make check` runs
it when there's a build and the cross compiler.

## `riscos/`: programs for the Pi

`riscos/build.sh` (or `make riscos-tests`) builds them against this repo's
library, converts them to AIF and zips them with Obey files as
`riscos/out/UnixLibTests.zip`. What each one does and what to report is in
`riscos/ReadMe`.
