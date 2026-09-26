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
| `host/fake` | The fake RISC OS, shared by both: `swis.h`/`kernel.h` (a variadic `_swix`), `internal/*.h` (the few UnixLib internals used), `DRender.h` (fake DigitalRenderer), `riscos.c`/`riscos.h` (SharedSoundBuffer and StreamManager playing in simulated time, `clock`, `pthread_yield`, `getenv`), `prelude.h` (renames those calls to the fakes) | |

Run one with `host/dsp/run.sh` or `host/midi/run.sh`. The midi test has its
own fake SWIs in `test_midi.c`; it only uses the headers from `fake/`.

Before trusting a new check, break the code it guards and see it fail.

Read the "Traps" section of `docs/MAINTAINING.md` before editing the fakes
(header order in `prelude.h`, pointers below 4 GB).

## `riscos/`: programs for the Pi

`riscos/build.sh` (or `make riscos-tests`) builds them against this repo's
library, converts them to AIF and zips them with Obey files as
`riscos/out/UnixLibTests.zip`. What each one does and what to report is in
`riscos/ReadMe`.
