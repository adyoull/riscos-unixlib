# riscos-unixlib

UnixLib (the C library of the GCCSDK GCC 10 `arm-riscos-gnueabihf` toolchain)
with the fixes made while porting programs to RISC OS: OpenTTD 14.1,
Warzone 2100 2.3.9 and the riscos-mesa SDL2 work. One place for all UnixLib
changes, so every port links against the same library.

Base: `gcc4/recipe/files/gcc/libunixlib` from
[jhamby/riscos-gccsdk](https://github.com/jhamby/riscos-gccsdk) at
`64c6f81` (2023-08-11). The first commit is that copy, unchanged; every change
after it is a separate commit.

## What's changed

| Area | Files | Change |
|---|---|---|
| Wide characters | `wchar/wmissing.c`, `wchar/wctype.c` | `wctype`, `iswctype`, `wcscoll`, `wcsxfrm`, `wcs(n)casecmp`, `wcsto*`, `swprintf`, `wcsftime`, `putwc`/`getwc`/`ungetwc`… were stubs that printed "Not implemented" and aborted. libstdc++'s `std::locale` set-up calls `wctype()`, so **any C++ program using iostreams or locales stopped at start-up**. Now simple 8-bit (Latin-1) versions. |
| Clock | `time/clk_gettime.c` | `CLOCK_MONOTONIC` is interpolated inside the centisecond with the HAL counter (`OS_Hardware` 19/20/21), so `std::chrono::steady_clock` and SDL timing are sub-microsecond instead of 10 ms steps. Falls back to centiseconds if the HAL values look wrong. New internal `__ul_monotonic_ns()`. |
| Sleeping | `signal/sleep.c` | `nanosleep` sleeps to sub-centisecond accuracy using the new clock; a bad `timespec` now returns `EINVAL`. |
| Memory | `stdlib/alloc.c` | On EABI, large `malloc`s no longer use `mmap` (each mapping was an ARMEABISupport `mmap#N` dynamic area that was left behind after exit). They come from the heap dynamic area. |
| Sound: exit bug | `sound/dsp.c` | Every UnixLib program's exit stopped DigitalRenderer, so quitting *any* UnixLib program cut off another program's sound. Now only the program that played stops it. Opening `/dev/dsp` or changing its settings no longer resets another program's sound either. |
| Sound: mixing | `sound/dsp.c` | `/dev/dsp` plays through **SharedSoundBuffer / StreamManager** when they're loaded: mixed with other programs' sound, any rate resampled. All common OSS formats (16-bit LE/BE, 8-bit signed/unsigned, µ-law, mono/stereo) and the usual ioctls. Falls back to DigitalRenderer. |
| Sound: default format | `sound/dsp.c` | The default format was A-law by mistake (16-bit was intended). |
| MIDI | `sound/midi.c` | New **`/dev/midi`**: raw MIDI bytes go to a MIDISynth module (proposed, see [docs/MIDISYNTH-MODULE.md](docs/MIDISYNTH-MODULE.md)) or the RISC OS MIDI module. |

Details and reasons: [CHANGELOG.md](CHANGELOG.md). Sound details:
[docs/SOUND.md](docs/SOUND.md). Known UnixLib problems
found by the ports but **not fixed yet**: [docs/TODO.md](docs/TODO.md).

Used by: riscos-openttd (14.1-riscos1 onwards), riscos-warzone2100
(riscos2 onwards). riscos-mesa's own libraries don't need it, but programs
linked with them usually do (`build/TOOLCHAIN.md` there says so).

## Using it

### Patch a GCCSDK checkout before building the toolchain

`patches/unixlib-riscos.diff` is the whole change set against a GCCSDK
checkout (paths `gcc4/recipe/files/gcc/libunixlib/...`):

```sh
patch -d riscos-gccsdk -p1 < patches/unixlib-riscos.diff
```

Use `patch`, not `git apply`: run inside another git repo, `git apply`
silently skips paths outside that repo.

It is the same change as riscos-openttd's
`patches/unixlib/unixlib-riscos-openttd.diff` (only the `index` lines
differ), so either file works.

### Rebuild only libunixlib.a for an installed toolchain

No need to rebuild GCC. `build/build-unixlib.sh` runs GCCSDK's
`reconf-libunixlib` steps and configures UnixLib on its own with your
installed cross compiler:

```sh
GCC_SRC=~/src/gcc-10.2.0 \
GCCSDK_SRC=~/src/riscos-gccsdk \
GCCSDK_ENV=~/gccsdk/env \
INSTALL=yes build/build-unixlib.sh
```

- Needs `autoconf2.69`, automake 1.11 (`aclocal-1.11`, `automake-1.11`) and
  perl. About 40 s on 2 cores.
- Default `CFLAGS` are `-g -O2 -fstack-clash-protection` (the flags the
  OpenTTD and Warzone toolchains use).
- Output: `build/work/build/.libs/libunixlib.a`. `INSTALL=yes` copies it
  into `$GCCSDK_ENV/arm-riscos-gnueabihf/lib/`. Only the static library is
  built; the ports link `-static`.
- Then relink your program.

Check: a library built this way from this repo is byte-for-byte identical
(`.text`, `.data`, `.rodata` of all 894 objects) to the `libunixlib.a` in
riscos-warzone2100's `gccsdk-gcc10.2-x86_64-linux-env.tgz` toolchain.

### Which programs get the changes

UnixLib is linked into each program. A program only gets these fixes when
it is **relinked** against this library (static builds, like the OpenTTD and
Warzone ports), or, for programs using the shared `libunixlib.so` from
`!SharedLibs`, when a new shared library is installed. Most PackMan programs
(ffplay, for example) are built with GCCSDK's GCC 4.7.4 and use *that*
toolchain's shared UnixLib, so they need the change taken into GCCSDK
(`patches/unixlib-sound.diff` is the sound part on its own, for that) and a
new `!SharedLibs` UnixLib release.

### Prebuilt

Releases carry `libunixlib.a` (static, `arm-riscos-gnueabihf`, built as
above). Drop it into `<env>/arm-riscos-gnueabihf/lib/` and relink.

## Porting notes (UnixLib behaviour ported programs trip over)

- `popen()` / `system()` run a `*command`, not a Unix shell: `popen("which
  x")` gives "File 'which' not found". Avoid them in ports.
- `getenv("Name$Var")` reads RISC OS system variables.
- There is no `dlopen`, `open_memstream`, `pthread_mutex_timedlock`,
  pthread barriers or `pthread_getcpuclockid`; no ELF TLS
  (`__aeabi_read_tp`).
- In a Wimp task, `sleep`/`usleep`/`nanosleep` busy-wait without calling
  Wimp_Poll, so the desktop freezes for the whole sleep (see TODO).
- Sound: open `/dev/dsp` and write; with SharedSoundBuffer loaded several
  programs can play at once. `UnixLib$DSP` = `DigitalRenderer` forces the
  old output. MIDI: write raw bytes to `/dev/midi`.

## Licence

UnixLib is © UnixLib Developers and others under the licences in
[libunixlib/COPYING](libunixlib/COPYING) (mainly BSD-style, with some
LGPL/GPL-with-exception parts). The changes here are offered under the same
terms as the files they change.
