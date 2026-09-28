# Maintaining riscos-unixlib

How the repo fits together, how to build and test it, and the traps found
so far. Read this before changing anything.

## Layout

| Path | What |
|---|---|
| `libunixlib/` | UnixLib itself. First commit = GCCSDK's `gcc4/recipe/files/gcc/libunixlib` at `64c6f81`, unchanged; every change after that is its own commit. |
| `patches/` | **Generated** from the history by `tools/make-patches.sh`; don't edit by hand. `unixlib-riscos.diff` = everything; `unixlib-sound.diff` = the sound work only (for GCCSDK). |
| `build/` | `fetch-sources.sh`, `sources.conf` (pinned sources), `build-unixlib.sh`. `build/src` and `build/work` are not in git. |
| `tests/check.sh` | Everything checkable without RISC OS. `make check` runs it; so does GitHub Actions. |
| `tests/host/` | Host (PC) tests with a fake RISC OS; see `tests/README.md`. |
| `tests/emu/` | Built machine code run in an ARM emulator (Unicorn). |
| `tests/abi/` | Struct layouts and symbol names that compiled code depends on. |
| `tests/riscos/` | Test programs for the Pi, zipped as `UnixLibTests.zip`. |
| `tools/` | `elf2aif` (large-image fix), `mkrozip.py` (zips with RISC OS filetypes), `make-patches.sh`, `make-release.sh`. |
| `release/` | ReadMe and licence for the PThreadTicker zip. |
| `docs/` | `SOUND.md`, `MIDISYNTH-MODULE.md`, `THREAD-TICKER.md`, `LARGE-FILES.md`, `TODO.md` (known problems), this file. |

## Making a change

1. One fix per commit. Say in the message what broke, how it showed up, and
   why the fix is right. Author: Andrew Youll.
2. In the code, mark new or changed parts with a short `2026:` comment
   saying why (see `sound/dsp.c`, `unix/sync.c`). Keep UnixLib's GNU style:
   2-space indent, space before `(`, tabs for 8 columns.
3. New source file: add it to `libunixlib/Makefile.am` (the `*_src` lists;
   most are inside `if UNIXLIB_BUILDING_SCL ... else ... endif`, and the
   SharedCLibrary build must not get UnixLib-only files). New internal
   `__name` symbols that aren't API: add them to `libunixlib/vscript`
   (the `local:` list). New device: `DEV_*` and `NDEV` in
   `incl-local/internal/dev.h`, a row in `unix/dev.c` (same order as the
   numbers), a name in `__sfile[]`, and `common/__stat.c`.
4. Add or update a host test if the code can run on a PC with fakes, and a
   `tests/riscos` program if it needs the Pi.
5. `make patches`, then `make check`. Update `CHANGELOG.md` (under
   "Unreleased") and, if behaviour changes, `README.md` / `docs/`.

## Building

```sh
make sources                 # GCCSDK + GCC source into build/src, checked
make lib                     # -> build/work/build/.libs/libunixlib.a
make install                 # also copies lib + headers into $GCCSDK_ENV
make riscos-tests            # -> tests/riscos/out/UnixLibTests.zip
```

`GCCSDK_ENV` is the installed cross toolchain (default `~/gccsdk/env`).
Host packages: `autoconf2.69`, `automake1.11`, `perl`, `python3`, a host
`gcc` (Debian/Ubuntu names).

### Toolchain

The cross compiler (GCCSDK GCC 10.2.0, `arm-riscos-gnueabihf`) is **not**
built here. Two ways to get it:

- Build it with riscos-warzone2100's `build/build-toolchain.sh`, which
  follows riscos-mesa's `build/TOOLCHAIN.md` (about 10 minutes on 2 cores).
  That script also applies this repo's UnixLib patch.
- Use the prebuilt `gccsdk-gcc10.2-x86_64-linux-env.tgz` (x86-64 Linux; the
  sha256 is in `build/sources.conf`). It must be unpacked in `/root`
  (`tar xzf … -C /root` gives `/root/gccsdk/env`): the path is built in.
  **To do:** attach it to a GitHub release so it can be downloaded.

A library built from this repo is the same, object for object, as the one in
that toolchain as it was before the sound work (checked: code and data of
all 894 objects).

## Testing

- `make check` must pass before every push (GitHub runs it too).
- Host tests can't catch everything: SWI calls are faked. Anything touching
  RISC OS behaviour needs a Pi run of `UnixLibTests.zip` before a release,
  and the result noted in `CHANGELOG.md`.

## Releases

Version numbers are **UnixLib versions**, continuing from the GCCSDK
UnixLib the repo is based on (5.0; `AC_INIT` in `libunixlib/configure.ac`
and `libunixlib/doc/UnixLib/Help` carry the number, change both). A PATCH
release (5.0.1, 5.0.2...) is fixes and small additions; bigger additions
(new devices, many new functions) make a MINOR one (5.1). If we move to a
newer GCCSDK UnixLib, continue from its number. The first releases were
numbered 0.1.x (`v0.1.0` = `22511f2`, `v0.1.1-rc1`).
Anything not yet run on RISC OS is released as a **pre-release**
(`vX.Y.Z-rcN`, marked "pre-release" on GitHub) and re-released without
the suffix after the Pi tests pass.

1. Move "Unreleased" to a version heading with the date; commit.
2. Set the version in `configure.ac` and `doc/UnixLib/Help`; `git tag -a vX.Y.Z -m "UnixLib X.Y.Z (riscos-unixlib)"`.
3. `make release TAG=vX.Y.Z`: builds from clean, runs `make check`, and
   puts the release files in `out/release/vX.Y.Z/`: `libunixlib.a`,
   `patches/*.diff`, `UnixLibTests.zip`, `PThreadTicker-<version>.zip`
   (for RISC OS users: a !System to merge, ReadMe, Licence; text in
   `release/`) and `SHA256SUMS`.
4. Push the commit and the tag; create the GitHub release from the tag
   with those files (`gh release create vX.Y.Z out/release/vX.Y.Z/*
   --notes-file ...`, plus `--prerelease` for an rc).

The module's own version is in its help string (`module/pthticker.s`);
bump it when the module changes.

## Moving to a newer GCCSDK

Our changes are commits on top of one import commit, so moving to a newer
UnixLib is "import it, then replay ours".

```sh
make sources GCCSDK_COMMIT=<new commit>     # or edit build/sources.conf
OLD=$(tools/import-commit.sh)               # the current import commit
git checkout -b gccsdk-<short> $OLD
rm -rf libunixlib
cp -r build/src/riscos-gccsdk/gcc4/recipe/files/gcc/libunixlib libunixlib
git add -A libunixlib
git commit -m "Update UnixLib to GCCSDK <short>"
git rebase --onto HEAD $OLD main
```

The import commit's message must start `Import UnixLib` or
`Update UnixLib`: `tools/import-commit.sh` finds the newest such commit,
and the patch scripts diff against it.

(`make sources` only checks out the parts it needs; add
`gcc4/recipe/files/gcc/libunixlib` with
`git -C build/src/riscos-gccsdk sparse-checkout add gcc4/recipe/files/gcc/libunixlib`.)

- Resolve conflicts one commit at a time; each of our commits says what it
  fixes, so you can tell whether GCCSDK has fixed it too (then drop ours).
- Update `GCCSDK_COMMIT` in `build/sources.conf`, the "Base" line in
  `README.md` and a CHANGELOG entry. `tools/make-patches.sh` finds the new
  import commit itself; check `SOUND_FROM`/`SOUND_TO` there (they are
  hashes, which the rebase changes).
- `make patches`, `make check`, `make lib`, then the Pi tests.
- Rewriting history like this breaks existing clones: push the result as a
  new branch, or a new major version, and say so in the release notes.

## Traps found so far

- **Use `patch -p1`, not `git apply`,** to apply `patches/*.diff` to a
  GCCSDK tree that lives inside another git repo: `git apply` silently skips
  paths outside that repo.
- **`LIBTOOLIZE=true`** in `build-unixlib.sh` is deliberate: letting
  autoreconf run the system's libtoolize installs libtool 2.4.7 over GCC's
  2.2.7a and the build stops with "Version mismatch error".
- **automake 1.11 and autoconf 2.69** are what GCCSDK's own scripts use;
  other versions haven't been tried.
- **Host test fakes:** `tests/host/fake/prelude.h` renames `pthread_yield`,
  `clock` and `getenv` to fakes, and must include the system headers
  *first*: glibc declares `pthread_yield` with `__REDIRECT` to
  `sched_yield`, and a macro defined before the header silently turns the
  fake into `sched_yield` (the test then hangs).
- **Host tests pass pointers through `int` SWI registers,** as on RISC OS.
  On a 64-bit PC anything passed to a fake SWI must be below 4 GB: build
  with `-no-pie` and keep such buffers static, not on the stack.
- **`dsp.c` is copied into `tests/host/dsp/out/`** before compiling, so its
  `#include "DRender.h"` finds the fake instead of the real inline-assembler
  one next to it.
- **RISC OS test programs ship as AIF** (`elf2aif -e`), not ELF: the ELF
  files had problems on the Pi.
- **Unaligned loads fault on RISC OS** (Linux hides it). See the README's
  porting notes.
- **UnixLib code runs in every program.** Anything at exit (`_exit` in
  `unix/unix.c`) must only undo what *this* program did: the old
  `__dsp_exit` switched off everybody's sound.
- **The thread ticker's routines run from the PThreadTicker module or
  RMA,** never from the program: the ticker fires whichever task is paged
  in. They are one macro, `incl-local/internal/ticker.s`, assembled into
  the module (`module/pthticker.s`) and into UnixLib (`pthread/_context.s`,
  copied into the RMA block when the module isn't loaded). Keep them
  position independent (only `ip`-relative data, `ADR` within the macro,
  SWIs; no literal pools, no branches out), under 320 bytes (the assembler
  checks), and never let the handler touch application space before the
  upcall/key check. The RMA block offsets they use and the module's
  interface table are an interface between UnixLib and the module, which
  are released separately: don't move them; bump the interface version
  for incompatible changes (docs/THREAD-TICKER.md). The block layout is
  written twice, in `incl-local/pthread.h` and `asm_dec.s`; `pthinit.c`
  checks at compile time that they agree. `make check` runs the built
  code in an emulator (`tests/emu`).
- **Don't change SharedUnixLibrary** (`module/sul.s`) here: it and its SWIs
  belong to GCCSDK, and a changed module under the same name and version
  would be confusing on users' machines. Things that need a module go in
  our own (like PThreadTicker).
- **Don't change what compiled code depends on.** Programs, static
  libraries (libstdc++ in the toolchain, devkits) and objects built with
  older headers get linked with new UnixLib. Never change a public struct
  layout or a function's arguments under an existing symbol name: give the
  new version a new name and map to it in the header (as `struct stat64`
  and `__unixlib_stat64`, docs/LARGE-FILES.md), and keep the old symbol.
  `tests/abi/check.sh` checks the layouts and symbols; if it fails,
  update `expected-*.txt` only for new modes or new names, never for the
  default mode.
- **Incremental builds and assembler files.** Automake doesn't track what
  `.s` files include. `Makefile.am` now makes every assembler object depend
  on `asm_dec.s`, the macro files and the two headers they include; if you
  add another shared include, add it to `unixlib_asm_deps`. A mix of old
  and new objects once overran the pthread RMA block and hung a Pi.
  `tools/check-lib.sh` (run by every build) and a start-up check in
  `pthinit.c` now catch that. When in doubt: `make clean; make lib`.
- **`PTHREAD_UNSAFE`** blocks thread switches until the function returns
  and keeps one global return address; don't call code that may wait for
  another thread inside it (that was the atexit abort).

## Keeping in step with other projects

Requests to and from the other ports travel as "handoff" notes. They live
beside the repo (`RiscOS/UnixLib/handoffs/`), not in it: `.gitignore`
excludes `handoffs/` and any `*handoff*.md`. Anything in one that the repo
needs goes into a commit message, `CHANGELOG.md` or `docs/`.

- riscos-openttd and riscos-warzone2100 carry a copy of the UnixLib patch
  (`patches/unixlib/*.diff`); after a change, send them the new
  `patches/unixlib-riscos.diff`.
- `tools/elf2aif` is a copy of riscos-openttd's `tools/elf2aif`.
- `sound/dsp.c` uses the same SharedSoundBuffer SWIs as the SDL2 RISC OS
  audio driver (riscos-openttd / riscos-mesa) and RDPClient.
- GCCSDK upstream: `patches/unixlib-sound.diff` (and the fsync / atexit
  fixes) should be offered there once tested; PackMan programs only get
  them through GCCSDK.
