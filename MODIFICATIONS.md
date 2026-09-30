# riscos-unixlib - every change from GCCSDK UnixLib, and why

This document lists **every** difference between `libunixlib/` and the
UnixLib in GCCSDK. For each change it gives the problem, the evidence, what
was changed, why it was done that way, what it means for programs, and how
it was checked. It is meant to let someone who was not involved follow, and
challenge, each decision.

The exact source changes are also in `patches/unixlib-riscos.diff` (unified
diff against unchanged GCCSDK UnixLib: 48 modified files and 7 added files).
`patches/unixlib-sound.diff` is the sound part (S1-S5) on its own. Each
change is also its own git commit, with the reasons in the commit message;
the commits are named below.

---

## 1. What the source is based on

| | |
|---|---|
| Upstream | GCCSDK, `gcc4/recipe/files/gcc/libunixlib` |
| Repository | https://github.com/jhamby/riscos-gccsdk (git mirror of `svn://svn.riscos.info/gccsdk/trunk`) |
| Commit | `64c6f81` (2023-08-11), pinned in `build/sources.conf` |
| UnixLib version there | 5.0 (`AC_INIT` 5.0, libtool `-version-info 5:0:0`) |
| SharedUnixLibrary there | 1.16 |
| Import commit here | `13eea91` "Import UnixLib from GCCSDK (jhamby/riscos-gccsdk 64c6f81)", the tree unchanged |

### File inventory (1334 files in `libunixlib/`)

| Status | Files |
|---|---|
| Byte-identical to GCCSDK `64c6f81` | 1279 |
| Modified (listed below) | 48 |
| Added | 7 |
| Removed relative to upstream | 0 |

| File | Change |
|---|---|
| `wchar/wctype.c`, `wchar/wmissing.c` | W1 wide-character functions |
| `time/clk_gettime.c` | T1 high-resolution `CLOCK_MONOTONIC` |
| `signal/sleep.c` | T2 `nanosleep` accuracy |
| `stdlib/alloc.c` | A1 no `mmap` for large blocks on EABI |
| `sound/dsp.c` | S1 exit bug, S2 default format, S3 SharedSoundBuffer output, S4 empty block |
| `sound/midi.c` (**new**), `common/__stat.c`, `unix/unix.c` | S5 `/dev/midi` |
| `unix/sync.c` | F1 `fsync` on read-only files, `fdatasync` |
| `stdlib/atexit.c` | X1 atexit handlers with thread switching allowed |
| `sched/sched_prio.c` (**new**), `include/sched.h` | P1 `sched_get_priority_min/max` |
| `pthread/schedparam.c`, `pthread/newnode.c` | P2 `pthread_setschedparam` |
| `pthread/ticker.c` (**new**), `incl-local/internal/ticker.s` (**new**), `module/pthticker.s` (**new**), `pthread/_context.s`, `pthread/context.c`, `pthread/pthinit.c`, `incl-local/pthread.h`, `incl-local/internal/asm_dec.s`, `sys/_syslib.s` | K1-K4 thread ticker |
| `include/sys/stat.h`, `incl-local/sys/stat.h`, `unix/stat64.c` (**new**), `unix/stat.c`, `unix/lstat.c`, `unix/fstat.c`, `unix/scl_fstat.c` | L1 `struct stat64` with a 64-bit size |
| `unix/ul_lseek.c`, `stdio/fseeko.c`, `stdio/ftello.c`, `stdio/fgetpos.c`, `stdio/fsetpos.c` | L2 64-bit seeking to 4GB-1 |
| `unix/truncate.c` | L3 `truncate64`, `ftruncate64` |
| `sys/mmap64.c` (**new**), `include/sys/mman.h` | L4 `mmap64` |
| `include/unistd.h` | F1 (`fdatasync`), L3 (declarations and redirects) |
| `unix/dev.c`, `incl-local/internal/dev.h` | S5 (device table), L2 (`__fslseek64`), R2 (`read()` into a stack) |
| `time/stdtime.c`, `incl-local/internal/os.h`, `incl-local/sys/socket.h`, `common/env.c`, `locale/iconv.c`, `stdio/err.c`, `netlib/scl_getservbyname.c`, `netlib/scl_getservbyport.c`, `resolv/scl_gethostbyname.c` | R1 inline SWI wrappers (`ctime` bad pointer) |
| `configure.ac`, `doc/UnixLib/Help` | V1 version number |
| `Makefile.am`, `vscript` | B1 build rules and symbol visibility for the above |

The modified files keep their original copyright lines. Code added to them
is marked with a `2026:` comment. The 7 new files carry
"Copyright (c) 2026 UnixLib Developers" and are under UnixLib's licence,
like the rest of the library.

**SharedUnixLibrary is not changed.** `module/sul.s` is GCCSDK's 1.16,
byte-identical (see K4 and section 7).

**To reproduce the inventory:** `git diff --stat 13eea91 HEAD -- libunixlib`
in this repository. Or fetch GCCSDK at `64c6f81` and compare its
`gcc4/recipe/files/gcc/libunixlib` with `libunixlib/`. `make check` confirms
that the patches apply to an unchanged GCCSDK UnixLib.

### Where the changes come from

The changes were first made in separate RISC OS ports, as patches in each
port's own tree, and were gathered here so every port links the same
library:

- OpenTTD 14.1: W1, T1, T2, A1.
- Warzone 2100 2.3.9: F1, X1, the problem behind K1-K4, and the crashes
  behind R1 and R2.
- riscos-mesa (SDL2, OpenAL Soft): P1, P2.
- Sound (S1-S5) and large files (L1-L4) were written here.

L1-L4 were written from UnixLib's own existing LFS declarations and stubs,
following the glibc conventions UnixLib's headers already use. The only
outside material read was RISC OS Open's published description of how
SharedCLibrary handles 64-bit file pointers: the 2011 forum announcement and
the wiki page "Migrating C software to 64-bit file pointers". That is prose;
no RISC OS source code was used.

---

## 2. C library changes

### W1. Wide-character functions (`wchar/wmissing.c`, `wchar/wctype.c`) - commit `08b3ce7`

**Problem.** `wctype`, `iswctype`, `iswupper`/`iswlower`/`iswblank`,
`wcscoll`, `wcsxfrm`, `wcscasecmp`/`wcsncasecmp`, the `wcsto*` family,
`swprintf`, `wcsftime` and `putwc`/`getwc`/`ungetwc` were stubs. They printed
"Not implemented" and called `abort()`.

**Evidence.** libstdc++'s `std::locale` set-up calls `wctype()`, so any C++
program that used iostreams or locales stopped at start-up (OpenTTD).

**Change.** Simple 8-bit (Latin-1) versions, matching UnixLib's existing
`ctype` tables. The `isw*` functions now return 0 for characters outside
0-255 instead of indexing past the end of the `ctype` tables.

**Why this way.** UnixLib's locales are 8-bit. Full Unicode classification
would need tables UnixLib doesn't have, and nothing in the ports needed it.

**Effect.** Programs that called these functions no longer abort. Characters
above 255 are classified as "none of the above".

**Verification.** In use in the OpenTTD 14.1 port on RISC OS.

### T1. High-resolution `CLOCK_MONOTONIC` (`time/clk_gettime.c`) - commit `fa59226`

**Problem.** `clock_gettime(CLOCK_MONOTONIC)` came from `clock()`, which
reads the RISC OS monotonic timer: centisecond steps.

**Evidence.** Frame pacing through `std::chrono::steady_clock` and SDL
jittered by up to 10 ms (OpenTTD).

**Change.** The position within the current centisecond is read from the HAL
counter that generates the centisecond tick (`OS_Hardware` 19 CounterRate,
20 CounterPeriod, 21 CounterRead):

- the centisecond count is read before and after the counter, and again if
  they differ;
- the result never goes backwards, because the counter can reload just
  before the centisecond count increments.

It falls back to centiseconds if any of these calls fails, or if the
counter's period isn't one centisecond at its rate. The new internal
function `__ul_monotonic_ns()` is used by T2 as well.

**Why this way.** The HAL counter is the only finer timer every RISC OS 5
machine has, and it only needs to be read, never programmed.

**Effect.** Sub-microsecond `CLOCK_MONOTONIC`. `CLOCK_REALTIME` and
`clock()` are unchanged.

**Verification.** In use in the OpenTTD port on RISC OS.

### T2. `nanosleep` accuracy (`signal/sleep.c`) - commit `1628218`

**Problem.** `nanosleep` rounded up to whole centiseconds. An invalid
`timespec` set `EINVAL` but carried on sleeping.

**Change.**
- Sleeps of 20 ms or more sleep all but the last centisecond as before.
- The rest is waited out against `__ul_monotonic_ns()` (T1), calling
  `pthread_yield()`.
- An invalid `timespec` returns -1 with `EINVAL`.
- When interrupted, `rem` gets the time left.

**Effect.** Short sleeps are accurate. The final wait keeps the CPU busy
(yielding to other threads, not to other tasks); see "Still to be done".

**Verification.** In use in the OpenTTD port.

### A1. No `mmap` for large blocks on EABI (`stdlib/alloc.c`) - commit `98a9287`

**Problem.** `malloc` served large requests with `mmap`. On EABI each mapping
is an ARMEABISupport `mmap#N` dynamic area.

**Evidence.** Those dynamic areas were still there after OpenTTD quit.

**Change.** `DEFAULT_MMAP_MAX` is 0 on `__ARM_EABI__`, so large blocks come
from the heap dynamic area. UnixLib removes that dynamic area when the
program exits.

**Effect.** No leaked dynamic areas. Freed large blocks go back to the heap
rather than straight to the OS.

**Verification.** In use in the OpenTTD port.

### F1. `fsync` on read-only files; `fdatasync` (`unix/sync.c`, `include/unistd.h`) - commit `751de68`

**Problem.** `fsync()` on a descriptor opened only for reading returned -1
with `EBADF`. glibc and the BSDs return 0.

**Evidence.** PhysFS 2.0.3 calls `fsync` before `close` and gives up if it
fails, so read-only files were never closed. Warzone 2100 reported "Error
closing config: Bad file descriptor", and then "This file is already open"
on every save.

**Change.** `fsync` returns 0 for such a descriptor. `fdatasync()` is added,
and is the same as `fsync` on RISC OS (OpenTTD and others had worked around
its absence).

**Effect.** Nothing that used to work behaves differently.

**Verification.** In use in the Warzone 2100 port. Pi test `FsyncRO` (not
yet run).

### X1. atexit handlers run with thread switching allowed (`stdlib/atexit.c`) - commit `7d875c1`

**Problem.** `__cxa_finalize` held off thread switching (`PTHREAD_UNSAFE`)
for its whole walk of the handler list. So atexit handlers and C++ static
destructors ran with switching blocked.

**Evidence.** Warzone 2100 aborted as it quit. The path was `exit` →
`__cxa_finalize` → `systemShutdown` → `SDL_WaitThread` → `pthread_join` →
`pthread_yield`, which reached `__pthread_fatal_error` because the switching
semaphore was set.

**Change.** Switching is held off only while the list is read and an entry is
marked as called. Handlers run with switching allowed. `exit()`'s own comment
already said handlers should not run with threads disabled.

**Effect.** A handler may wait for another thread.

**Verification.** In use in the Warzone 2100 port. Pi test `ExitJoin` (not
yet run).

### P1. `sched_get_priority_min` / `sched_get_priority_max` (new `sched/sched_prio.c`, `include/sched.h`) - commit `04408eb`

**Problem.** UnixLib has `pthread_setschedparam`, so OpenAL Soft's CMake check
turns on its real-time priority code. That code then calls
`sched_get_priority_min(SCHED_RR)`, which didn't exist, so it failed to link
(riscos-mesa).

**Change.** Both functions exist and are declared in `<sched.h>`. UnixLib's
scheduler is a round robin that ignores priorities, so every known policy has
the single priority 0. An unknown policy gives -1 with `EINVAL`.

**Verification.** Pi test `Sched` (not yet run).

### P2. `pthread_setschedparam` (`pthread/schedparam.c`, `pthread/newnode.c`) - commit `145ee8a`

**Problem.**
- The policy check was written with `||`, so every policy failed with
  `EINVAL` and the function never did anything.
- New threads' policy and priority were never initialised, so
  `pthread_getschedparam` returned leftover heap contents.

**Change.**
- `SCHED_OTHER` with priority 0 is accepted and recorded.
- `SCHED_FIFO`, `SCHED_RR` and `SCHED_SPORADIC` fail with `ENOTSUP`, as
  POSIX allows, since the scheduler doesn't have them.
- Anything else gives `EINVAL`.
- New threads start as `SCHED_OTHER`, priority 0.

**Effect.** Callers that asked for `SCHED_RR` (OpenAL) got an error before
and still do; it is now the right error.

**Verification.** Pi test `Sched` (not yet run).

### R1. Inline SWI wrappers: every register and memory the SWI changes - commit `c98c781`

**Problem.** UnixLib calls many SWIs through small inline functions that put
arguments in registers with `register ... __asm ("rN")` variables. GCC
assumes that any register not listed as an output or clobber keeps its
value across the SWI, and that memory isn't read or written unless it is
told so. Several wrappers left out registers the SWI changes:

- `Territory_ConvertDateAndTime` (`time/stdtime.c`) returns R2 = bytes left
  in the buffer, but R2 (the buffer) was an input only.

**Evidence.** Warzone 2100 2.3.9-10 aborted at start-up on one Pi 4 in
`strlen` at &27, called with the result of `ctime()`. In the linked
program, `__standard_time` loaded the buffer into R2, called the SWI, and
returned R2. The static buffer is 64 bytes and the date string with its
terminator is 25, so R2 came back as 39 = &27. With a caller's 26-byte
buffer (`ctime_r`, `asctime_r`) it returned 1. Whether reading near address
0 aborts depends on the machine, which is why it crashed on one Pi and not
another.

**Change.** All 69 inline SWI wrappers were checked against the PRM:
- `Territory_ConvertDateAndTime`: R2 is an output ("+r"). R3 and R4 are
  treated the same way rather than assumed preserved.
- `OS_GBPB` 2 and 4 (`internal/os.h`): R2 returns the address after the
  last byte transferred, so it is an output. The write also reads memory.
- `OS_GetEnv` (`stdio/err.c`, SharedCLibrary build): R1 and R2 are outputs.
- SharedCLibrary `gethostbyname`: R0 (status) and R1 (the hostent) are
  outputs. As far as the compiler knew they were never set. It now
  returns NULL on failure, as POSIX says; before, it returned whatever was
  in R1.
- Wrappers that pass pointers to SWIs that read or write memory list
  `"memory"`: file names and buffers in `internal/os.h`, all the Socket
  SWIs, `iconv`, `OS_ReadVarVal` and `OS_SetVarVal`. Otherwise GCC may
  move a store to a buffer after the SWI that reads it, or keep a value in
  a register that the SWI has overwritten in memory.
- Both wrapper headers now state the rule in a comment.

**Why this way.** The wrappers stay as they are, with only their register
and clobber lists corrected. Rewriting them with `_swix` would change far
more code.

**Effect.**
- `ctime`, `ctime_r`, `asctime` and `asctime_r` return their buffer.
- The OS_GBPB and OS_GetEnv mistakes were latent: nothing in the 5.0.2
  build used the stale registers.
- No interface changes: the library exports exactly the same 2160
  symbols as 5.0.2, and `tests/abi/check.sh` passes.
- Machine code changed only in `stdtime.o` and `dev.o`, plus register
  allocation in `rename.o`, `tty.o`, `vfork.o` and `symlink.o` (a value
  reloaded from memory after a SWI instead of kept in a register).

**Verification.** `tests/emu/swi_test.py` links the library and runs
`__standard_time` in the Unicorn emulator, with a fake
Territory_ConvertDateAndTime that returns R2 = bytes left and changes R3
and R4. It checks the returned pointer and the text, for both a caller's
buffer and the static buffer. Against the unfixed library it fails, with
`ctime` returning &27, as in Warzone's crash. The SharedCLibrary files were
compiled by hand with `-mlibscl`. Not yet run on RISC OS.

**Upstream status.** Not reported. The same wrappers are in GCCSDK.

### R2. `read()` into a stack buffer maps the pages first (`unix/dev.c`) - commit `6b1c53a`

**Problem.** For EABI programs, ARMEABISupport gives each stack a dynamic
area whose pages are mapped in by its abort handler when first touched. A
SWI that writes into a stack page nobody has touched yet aborts in SVC
mode, and the program dies.

**Evidence.**
- Warzone 2100 2.3.9-7 died on one Pi 4 with "Fatal signal received: EMT
  trap", just after fontconfig wrote its cache. fontconfig `read()`s into
  stack buffers (`char buf[BUFSIZ]` in `fcxml.c`, an `FcCache` in
  `fccache.c`). It depends on how deep the stack had been used before, so
  it happened on one machine and not another.
- GCCSDK's own fontconfig port works around the same thing with a
  `memset` before the `read()`: "the read below causes a stack page fault
  from SVC mode when a SWI (probably OS_GBPB) is used to read a file to the
  stack" (`autobuilder/libraries/fontconfig/src.fcxml.c.p`, 2021).
- ARMEABISupport's source (GCCSDK `gcc4/riscos/armeabisupport`): the stack
  abort handler maps whatever page the fault address is in, whether the
  access was a read or a write.

**Change.** `__fsread` (which serves `read`, `readv`, and `fread` through
`read`) calls a new `touch_stack_pages` before OS_GBPB:
1. If the buffer ends above the current frame, `ARMEABISupport_StackOp` 2
   says whether it is in a stack, and 3 gives the stack's bounds.
2. One byte of each page of the part of the buffer that lies between the
   stack's base (above its guard pages) and top is read, from USR mode, so
   the abort handler maps it.

EABI builds only.

**Why this way.**
- *Only reading, not writing (GCCSDK's `memset`):* the abort handler maps
  the page either way, and reading can't change the caller's data or race
  with another thread.
- *Only stack pages:* a caller may pass a buffer larger than the memory
  behind it and rely on the file being short. Touching every page of such
  a buffer could fault where the SWI wouldn't have written. Every page
  between a stack's base and top belongs to that stack, so touching those
  is always safe.
- *In the library rather than in each program:* every program gets it,
  and fontconfig, PhysFS, expat and SDL all `read()` into buffers.

**Effect.** For a heap buffer, one extra SWI (StackOp 2, which fails) per
`read()` on a RISC OS file. For a stack buffer, two SWIs and one load per
page. A zero-length read or a buffer below the current frame costs nothing
extra.

**Verification.** `tests/emu/swi_test.py` makes the fake OS_GBPB fail if it
writes into a page of a fake stack that no USR-mode access has touched,
and checks:
- a 3-page buffer spanning 4 pages is fully touched before the SWI, and
  only those pages;
- the guard page isn't touched;
- a heap buffer isn't touched, and only StackOp 2 is called for it;
- a zero-length read calls no StackOp;
- the data arrives.

Against the unfixed library, no page is touched and the checks fail. Not
yet run on RISC OS: Chris Gransden's machine is where the crash was seen.

**Not covered.** Other SWIs that write into caller buffers can hit the
same problem if the buffer is on the stack: Socket_Recv and the other
Socket reads, OS_File loads, OS_Args/OS_FSControl results, and `readlink`.
See section 8.

---

## 3. Sound (`/dev/dsp`, `/dev/midi`)

### S1. Exiting stopped other programs' sound (`sound/dsp.c`) - commit `59fa9a5`

**Problem.** `_exit()` calls `__dsp_exit()` in every UnixLib program, and it
deactivated DigitalRenderer whether or not the program had played anything.
Opening `/dev/dsp`, or any ioctl on it, also deactivated and reconfigured
DigitalRenderer, even when another program owned it.

**Evidence.** Quitting any UnixLib program cut off the sound of another
program playing through `/dev/dsp`.

**Change.**
- `dsp.c` remembers whether this program activated DigitalRenderer. Exit and
  close only deactivate it then.
- Open and ioctls only record the settings.
- The buffer settings are applied when the program first writes. Only then is
  another program's session taken over (DigitalRenderer has one user at a
  time, as before).

**Verification.** Host test (`tests/host/dsp`, fake DigitalRenderer). Pi test
`ExitBug` (not yet run).

### S2. Default format (`sound/dsp.c`) - commit `4b1ab8f`

**Problem.** `__dspopen` set the format to 2, which is `AFMT_A_LAW`, not
`AFMT_S16_LE`. UnixLib's comment says the default is 16-bit.

**Evidence.** A program that wrote 16-bit samples without setting the format
had them played as 8-bit noise. Programs that set the format (SDL does) were
unaffected.

**Change.** The default is `AFMT_S16_LE`.

### S3. Output through SharedSoundBuffer / StreamManager (`sound/dsp.c`) - commit `e275d88`

**Problem.** DigitalRenderer serves one program at a time, at the rates the
hardware offers.

**Change.** When SharedSoundBuffer and StreamManager (John Duffell) are
loaded, `/dev/dsp` plays through them. These are the same SWIs RDPClient and
SDL2's RISC OS audio driver use. SharedSound mixes the stream with every
other program's sound and resamples any rate to the hardware rate.

- **Formats.** Everything written is converted to S16LE stereo: S16 LE/BE, U8,
  S8 and µ-law, mono or stereo, 4000-96000 Hz. Incomplete frames are kept
  for the next write.
- **Opening.** The stream opens on the first write (named after the program),
  and is paused until a fragment is queued.
- **Buffering.** The default is 8 fragments of 1024 frames (about 190 ms).
  `SETFRAGMENT` is honoured, from 20 ms to 2 s.
- **Writes.** Blocking writes wait for room. `O_NONBLOCK` writes take what
  fits, otherwise `EAGAIN`. A stream that stops playing for 2 s makes the
  write return short (or `EIO`) instead of hanging.
- **ioctls:** `SPEED`, `SETFMT`, `STEREO`, `CHANNELS`, `GETFMTS`, `GETBLKSIZE`,
  `SETFRAGMENT`, `GETOSPACE`, `GETODELAY`, `GETOPTR`, `SYNC`, `POST`,
  `RESET`/`HALT`, `GETCAPS`, `GET`/`SETTRIGGER`. Values are written back as
  OSS expects.
- **Closing.** `close()` lets queued sound finish (except with `O_NONBLOCK`).
  `_exit()` closes the stream at once.
- **Choosing the output.** `UnixLib$DSP=DigitalRenderer` forces the old path.
  `UnixLib$DSP=SharedSound` refuses to fall back: open fails with `ENODEV`.

**Why this way.** StreamManager copies each block into its own memory, so no
code runs from the program's memory under interrupts. Without the modules
nothing changes: DigitalRenderer is used, with S1.

**Getting the modules.** SharedSound is part of RISC OS. SharedSoundBuffer
and StreamManager come from `ssb.zip` on Andrew Sellors' RDPClient page,
https://orac.co.uk/software/rdpclient/rdpclient.html (merge its `!System`).
John Duffell's own site, now on the Internet Archive, has more details:
https://web.archive.org/web/20110920080106/http://www.duffell.riscos.me.uk/

**Verification.**
- `tests/host/dsp`: 124 checks against fake SharedSoundBuffer, StreamManager
  and DigitalRenderer modules.
- **Not yet run on RISC OS:** the Pi tests `Tone*`, `Mix` and `ExitBugSSB`.

### S4. Empty block when less than a frame is free (`sound/dsp.c`) - commit `9f04ddc`

A one-line fix to S3: a write never queues an empty block when less than a
frame of space is free.

### S5. `/dev/midi` (new `sound/midi.c`; `unix/dev.c`, `incl-local/internal/dev.h`, `common/__stat.c`, `unix/unix.c`, `vscript`) - commit `2d01327`

**Change.** A new device, `DEV_MIDI`, also available as `/dev/midi0` and
`/dev/midi00`. Programs write raw MIDI bytes and they play at once.

- **Where the bytes go.** To the MIDISynth module (`MIDISynth_Open`, `Write`,
  `Reset`, `Close`: a shared General MIDI software synth, proposed in
  `docs/MIDISYNTH-MODULE.md`). Otherwise to the RISC OS MIDI module
  (`MIDI_TxByte`, external or USB MIDI).
- **SWIs.** They are looked up by name, so no numbers are built in.
- **Choosing the output.** `UnixLib$MIDI=MIDISynth` or `UnixLib$MIDI=MIDI`
  allows only that one. With neither available, open fails with `ENODEV`.
- **Connections.** One connection per program, shared by its descriptors.
  The last close, or `_exit()` if the program never closed it, disconnects.
- **Stopping notes.** On disconnect, the synth module turns off that
  client's notes. On MIDI hardware, "all notes off" and "reset controllers"
  are sent on all 16 channels. `SNDCTL_SEQ_RESET` and `SNDCTL_SEQ_PANIC` do
  the same.
- **No MIDI input.**
- `stat` reports the device as a character device.

**Verification.**
- `tests/host/midi`: 20 checks.
- On a Pi without either module, the `MIDI` test gives `ENODEV`, as
  expected.
- Not yet played through a MIDI module on RISC OS.

---

## 4. Thread ticker (K1-K4)

Background, the investigation and the `UnixLib$TickerStats` fields are in
`docs/THREAD-TICKER.md`. In short: UnixLib switches threads with an
OS_CallEvery ticker. Wimp pre- and post-filters are meant to stop the ticker
while the program is paged out, because the ticker is system-wide.

**Evidence.** With Warzone 2100 running on a Pi 4, other tasks (Organizer,
Alarm) died with "Internal error: abort on instruction fetch". The ticker
fired while another task was paged in, and its handler was in Warzone's
application memory, so the CPU fetched code from the other task's memory.

### K1. The ticker code is not in application memory - commits `8c5c824`, `890b2e9`

The ticker handler, and later the filters, only use R12 (the pthread RMA
block) and SWIs. The handler checks that the current upcall handler is
SharedUnixLibrary's, with this program's key, before it touches application
memory.

It first ran from a copy in the RMA block (`8c5c824`). K4 then made a module
the main place for it, with the RMA copy kept as the fallback.

The RMA block is described twice: in C (`incl-local/pthread.h`) and in
assembler (`asm_dec.s`). `_Static_assert`s in `pthinit.c` stop the build if
the two disagree.

### K2. Stale objects can't overrun the block - commit `214412f`

**Evidence.** The block grew from 120 to 248 bytes. An incremental build kept
an old `_syslib.o` that still claimed only 120 bytes. The copy then ran past
the end of the block and corrupted the RMA. Warzone riscos14/15 hung a Pi 4
with "abort on data transfer" in a loop.

**Change.**
- `Makefile.am` makes every assembler object (and `crt0.o`, `gcrt0.o`,
  `sul.o`, `pthticker.o`) depend on the shared assembler definitions.
  automake doesn't track what `.s` files include.
- `sys/_syslib.s` exports the size it claims (`__pthread_callevery_block_size`).
- `__pthread_prog_init` stops with "UnixLib was built from mismatched
  objects" if that size isn't the C structure's size.
- An older `_syslib.o` doesn't define the symbol, so such a mix fails to
  link.
- `tools/check-lib.sh` checks a built library or program for the same
  thing.

### K3. Filters follow the task; statistics - commits `01d8356`, `fb74e2f` (the ticker parts), `723a08a`

**What was wrong.** The task handle was read once at start-up, before
`Wimp_Initialise`, and only read again while it was 0. The filters were
registered only when a thread was created. SDL creates threads before its
window, so the ticker could run with no filters for good.

**Change.** The ticker code in `_context.s` became C, in `pthread/ticker.c`.
- The task handle is read afresh (`Wimp_ReadSysInfo 5`) whenever the ticker
  starts, and every 64 thread switches (about 1.3 s; `pthread/context.c`).
- If the handle has changed, the filters are removed with the handle they
  were registered with, then registered for the new one.
- A failure to register the filters is counted, not fatal: the handler is
  safe anyway (K1).
- If `UnixLib$TickerStats` names a file, one line of counters is appended to
  it at exit. Nothing is written when the variable is unset.

The block is now 472 bytes.

**Verification (Pi 4, Warzone 2100 with the library and the module).**
- It ran for 17 minutes with other tasks running, and nothing crashed.
- The stats line showed `first_start=0`: the ticker had started with no
  filters, the H2 case in the doc. `filter_moves=1`: the periodic check
  registered them. `pre=9307 post=9315`: the filters ran thousands of times.
- Only 1 tick in 50,980 found another task paged in.

### K4. The PThreadTicker module (new `module/pthticker.s`, `incl-local/internal/ticker.s`) - commit `2b7f8f8`

**Change.** The five routines (handler, start, stop and the two filters) are
one macro, `internal/ticker.s`. It is assembled twice: into UnixLib, and into
a small new module, **PThreadTicker** (file `PThrTicker`, 648 bytes).

- **The module.** It has no SWIs, commands or service calls, so nothing
  needs allocating.
- **Finding it.** UnixLib finds it with `OS_Module 18` and reads an interface
  table straight after the module header. At &34 are the magic "PTTk", the
  version (1), and 7 routine offsets.
- **Fallback.** If the module isn't loaded, or the table doesn't match,
  UnixLib copies its own routines into the RMA block, as in K1.
- **Killing it.** Programs attach at start-up and detach at exit. The module
  refuses to be killed while any are attached.

**Why a module, and why not SharedUnixLibrary.**
- *A module rather than only the RMA copy:* running code from an RMA data
  block assumes the RMA is executable. RISC OS Open has discussed splitting
  the RMA so that data claimed with `OS_Module 6` could be made
  non-executable. Code in a module keeps working if that happens.
- *Not SharedUnixLibrary:* SUL and its SWIs belong to GCCSDK. For one day the
  routines were in a "SUL 1.17" with a new SWI (`fb74e2f`). That was
  withdrawn, because an unofficial module under the official name and
  version would confuse anyone who has it (see section 6).

**Effect.** The module is optional. Without it, programs work as with K1.
The RMA block offsets the routines use are now an interface between
separately released pieces (listed in `docs/THREAD-TICKER.md`), and must not
move.

**Verification.**
- `tests/host/ticker`: 34 checks, with fake SWIs.
- `tests/emu/ticker_test.py`: 77 checks. It runs the built module and
  UnixLib's copy in the Unicorn ARM emulator, covering the header, the table,
  attach/detach, refusing to be killed, and the routines themselves.
- On the Pi: the K3 run used the module (`via=module`).
- **Not yet run on RISC OS:** without the module (the RMA copy, which is the
  same code Warzone riscos16/17 ran), and refusing `*RMKill` while in use.

---

## 5. Files over 2GB (L1-L4)

Background and the compatibility table are in `docs/LARGE-FILES.md`.

RISC OS file pointers and extents are unsigned 32-bit, so a file can be up to
4GB-1 bytes (FileCore on RISC OS 5, FAT32). There is no 64-bit FileSwitch
interface.

UnixLib's `off_t` is a signed 32-bit `long`. Its Large File Support interface
(`_FILE_OFFSET_BITS=64`, `_LARGEFILE64_SOURCE`) existed but stopped at
2GB-1.

**The rule for all four changes:** nothing that was already compiled may
change. New layouts get new symbol names, mapped in the headers, and the old
symbols stay.

### L1. `struct stat64` with a 64-bit `st_size` - commit `f97de08`

**Problem.** `struct stat64` was identical to `struct stat`, with a signed
32-bit `st_size`. Sizes of 2GB and over came back negative.

**Change.**
- **Layout.** `struct stat64` has a 64-bit `st_size`: 72 bytes, with
  `st_size` at offset 32. Under `_FILE_OFFSET_BITS=64`, `struct stat` is the
  same. The default `struct stat` is unchanged: 64 bytes, `st_size` at 28.
- **New functions.** `__unixlib_stat64`, `__unixlib_fstat64` and
  `__unixlib_lstat64` fill the new layout, reading the RISC OS size as
  unsigned. The headers map `stat64`/`fstat64`/`lstat64` to them, and
  `stat`/`fstat`/`lstat` too under `_FILE_OFFSET_BITS=64`.
- **Old symbols kept.** `stat64`, `fstat64` and `lstat64` keep the old layout
  (asm labels in `stat.c`, `lstat.c`, `fstat.c` and `scl_fstat.c`).
- **SharedCLibrary build.** It gets `__unixlib_fstat64` too.

**Why this way.** Objects and libraries built with the old headers call
`stat64` with the smaller structure; the toolchain's `libstdc++.a` and
`libstdc++fs.a` do. Enlarging the structure under the same name would make
them overwrite 8 bytes past it.

### L2. 64-bit seeking to 4GB-1 - commit `95a33d4`

**Change.**
- **`lseek64`.** On a RISC OS file it uses the new `__fslseek64` (`unix/dev.c`).
  That works out the position in 64 bits from the unsigned pointer or extent,
  refuses negative positions (`EINVAL`) and positions past 4GB-1
  (`EOVERFLOW`), and passes the unsigned value to `OS_Args`. Other devices
  behave as before.
- **stdio.** `fseeko64` is `fseeko` with a 64-bit offset, using `lseek64`.
  `ftello64` and `fgetpos64` read the stream position back as unsigned.
- **`FILE` is unchanged.** Positions up to 4GB-1 fit in its 32-bit field.
- **Bug fixed.** `fsetpos64` converted the *pointer* it was given, not the
  position, so it moved to a meaningless place. It now calls
  `fseeko64(stream, *pos, SEEK_SET)`.

The 32-bit `lseek`, `fseeko`, `ftello`, `fgetpos` and `fsetpos` are
unchanged. `fpos_t` is 64-bit under `_FILE_OFFSET_BITS=64`, as it already
was.

### L3. `truncate64`, `ftruncate64` (`unix/truncate.c`, `include/unistd.h`) - commit `f769559`

**Change.** New functions with a 64-bit length: negative gives `EINVAL`, over
4GB-1 gives `EFBIG`. The headers map `truncate` and `ftruncate` to them
under `_FILE_OFFSET_BITS=64`. They are built for the SharedCLibrary variant
too. The 32-bit functions are unchanged.

### L4. `mmap64` (new `sys/mmap64.c`, `include/sys/mman.h`) - commit `40029ac`

**Problem.** `<sys/mman.h>` declared `mmap` with `off_t`, which is 64-bit
under `_FILE_OFFSET_BITS=64`, but the library's `mmap` takes a 32-bit offset.
Such programs passed the offset where `mmap` didn't look for it.

**Change.** `mmap` is declared with `__off_t`. Under `_FILE_OFFSET_BITS=64`
it maps to the new `mmap64`, which takes a 64-bit offset. The offset must fit
in 32 bits, otherwise `EOVERFLOW`.

**Effect.** New compiles are correct. Objects already compiled with
`_FILE_OFFSET_BITS=64` that call `mmap` still pass the offset wrongly, as
before, until they are recompiled.

### Verification of L1-L4

`tests/abi/check.sh` runs as part of `make check`. It checks:
- the structure layouts in each mode, against `tests/abi/expected-layout.txt`
  (the default mode must match 5.0.1);
- which symbols each call compiles to (`expected-calls.txt`);
- that the old symbols are still exported;
- that a C++ program links with the toolchain's libstdc++.

**On RISC OS (2026-09-29).** The Pi test `LargeFile` passed on SDFS with a 3GB
file:
- the size read back correctly through `fstat` and `stat`;
- data written and read back just past 2GB and at the last bytes;
- `lseek` to the end and relative to the current position;
- `fseeko`/`ftello`/`fgetpos`/`fsetpos` past 2GB;
- `EOVERFLOW` at 4GB and `EFBIG` at 5GB.

---

## 6. Version, build and symbols

### V1. Version number (`configure.ac`, `doc/UnixLib/Help`) - commits `d0bff5c`, `de2f01e` and the 5.0.3 release commit

`AC_INIT` and the Help file say **5.0.3**. Releases continue UnixLib's own
numbering from GCCSDK's 5.0. The libtool version stays `5:0:0`, because no
interface was removed.

### B1. `Makefile.am`, `vscript`

- **New sources.** The new files are added to the source lists:
  `pthread/ticker.c`, `sched/sched_prio.c`, `sound/midi.c`, `sys/mmap64.c`,
  `unix/stat64.c` and `internal/ticker.s`.
- **Assembler dependencies.** See K2.
- **The module.** It is built with the library: `bin_PROGRAMS = sul
  pthticker`.
- **`vscript`.** The new internal symbols (`__midi*`, `__pthread_ticker_*`,
  `__pthread_call_every_code*`, `__pthread_callevery_block_size`) are local,
  like the existing `__dsp*`.

Outside `libunixlib/`, the repository has its own build and test kit:
`build/`, `tests/`, `tools/` (including a fixed `elf2aif` for images over
32MB) and `Makefile`. None of it goes into the library.

---

## 7. Withdrawn

- **SharedUnixLibrary "1.17"** (`fb74e2f`, 2026-09-27). It put the ticker
  routines into SUL behind a new SWI, `SharedUnixLibrary_Ticker`. It was
  undone the same day by `2b7f8f8`, and `module/sul.s` is GCCSDK's 1.16
  again. SUL belongs to GCCSDK, and an unofficial version under its name
  would cause confusion. The routines went into PThreadTicker instead (K4).
- **The RMA copy as the only home of the ticker** (`8c5c824`). It is kept as
  the fallback when the module isn't loaded (K4).

---

## 8. Still to be done

**Not yet run on RISC OS:**
- the sound tests (`Tone*`, `Mix`, `ExitBug`, `ExitBugSSB`);
- `ExitJoin`, `FsyncRO`, `Sched`;
- a threaded program without PThreadTicker;
- `*RMKill PThreadTicker` refusal;
- R1 and R2 (on the machine where Warzone 2100 crashed).

**Stack pages and other SWIs (R2):** only `read()` on RISC OS files maps a
stack buffer's pages first. Socket reads, OS_File loads, `readlink` and
UnixLib's own stack buffers passed to SWIs could hit the same abort.

**Known problems** (details in `docs/TODO.md`), not caused by these changes:
- sleeping in a Wimp task doesn't multitask;
- `/dev/dsp` spins when its queue is full;
- `>` redirection can crash at start-up in a TaskWindow;
- `popen`/`system` run RISC OS commands;
- some functions are still missing;
- the scheduler has no real-time policies.

**Registering and reporting:**
- register the module name "PThreadTicker" with RISC OS Open;
- offer these changes to GCCSDK (not yet done; the patches are made for
  that). SharedUnixLibrary would stay unchanged.
