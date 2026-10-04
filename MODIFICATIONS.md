# riscos-unixlib - every change from GCCSDK UnixLib, and why

riscos-unixlib is an **unofficial fork** of the UnixLib in GCCSDK. It is
not made, released or supported by the GCCSDK developers, and its version
numbers (5.0.1 onwards) are its own releases, not GCCSDK's.

AI (Anthropic's Claude) has been used as a coding assistant on this fork.

This document lists **every** difference between `libunixlib/` and the
UnixLib in GCCSDK. For each change it gives the problem, the evidence, what
was changed, why it was done that way, what it means for programs, and how
it was checked. It is meant to let someone who was not involved follow, and
challenge, each decision.

The exact source changes are also in `patches/unixlib-riscos.diff` (unified
diff against unchanged GCCSDK UnixLib: 60 modified files and 8 added files).
`patches/unixlib-sound.diff` is the sound part (S1-S9) on its own. Each
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
| Byte-identical to GCCSDK `64c6f81` | 1266 |
| Modified (listed below) | 61 |
| Added | 8 |
| Removed relative to upstream | 0 |

| File | Change |
|---|---|
| `wchar/wctype.c`, `wchar/wmissing.c` | W1 wide-character functions, W2 `swprintf`/`wcsftime` formats, W3 long numbers in `wcsto*` |
| `wchar/wctype_l.c` | W4 range check in `isw*_l` |
| `time/clk_gettime.c` | T1 high-resolution `CLOCK_MONOTONIC`, T4 its last value read under a lock |
| `signal/sleep.c` | T2 `nanosleep` accuracy, T3 long sleeps and held-off thread switching |
| `stdlib/alloc.c` | A1 no `mmap` for large blocks on EABI, A2 a heap of several dynamic areas |
| `sys/brk.c`, `incl-local/unistd.h` | A2 a heap of several dynamic areas |
| `sound/dsp.c` | S1 exit bug, S2 default format, S3 SharedSoundBuffer output, S4 empty block, S6-S9 (fork children, second opens, READ ioctls, takeover), S10 fragments and `GETOPTR` |
| `sound/DRender.h` | R1 (`"memory"` on the sample-buffer calls) |
| `sound/midi.c` (**new**), `common/__stat.c`, `unix/unix.c` | S5 `/dev/midi`, S6 (fork children), S11 writes when MIDISynth is full |
| `unix/sync.c` | F1 `fsync` on read-only files, `fdatasync` |
| `stdlib/atexit.c` | X1 atexit handlers with thread switching allowed |
| `sched/sched_prio.c` (**new**), `include/sched.h` | P1 `sched_get_priority_min/max` |
| `pthread/schedparam.c`, `pthread/newnode.c` | P2 `pthread_setschedparam` |
| `pthread/ticker.c` (**new**), `incl-local/internal/ticker.s` (**new**), `module/pthticker.s` (**new**), `pthread/_context.s`, `pthread/context.c`, `pthread/pthinit.c`, `incl-local/pthread.h`, `incl-local/internal/asm_dec.s`, `sys/_syslib.s`, `sys/exec.c` | K1-K7, K9 thread ticker (`sys/_syslib.s` also A2) |
| `sys/vfork.c`, `sys/_vfork.s`, `incl-local/internal/unix.h` | K8 fork on EABI: the child's exit and the parent's stack |
| `include/sys/stat.h`, `incl-local/sys/stat.h`, `unix/stat64.c` (**new**), `unix/stat.c`, `unix/lstat.c`, `unix/fstat.c`, `unix/scl_fstat.c` | L1 `struct stat64` with a 64-bit size |
| `unix/ul_lseek.c`, `stdio/fseeko.c`, `stdio/ftello.c`, `stdio/fgetpos.c`, `stdio/fsetpos.c` | L2 64-bit seeking to 4GB-1 |
| `unix/truncate.c` | L3 `truncate64`, `ftruncate64` |
| `sys/mmap64.c` (**new**), `include/sys/mman.h`, `sys/mman-armeabi.c` | L4 `mmap64` |
| `unix/glob.c` | L5 `glob()` with `GLOB_ALTDIRFUNC` |
| `include/unistd.h` | F1 (`fdatasync`), L3 (declarations and redirects) |
| `unix/dev.c`, `incl-local/internal/dev.h` | S5 (device table), L2 (`__fslseek64`), R2 (`read()` into a stack) |
| `time/stdtime.c`, `incl-local/internal/os.h`, `incl-local/sys/socket.h`, `common/env.c`, `locale/iconv.c`, `stdio/err.c`, `netlib/scl_getservbyname.c`, `netlib/scl_getservbyport.c`, `resolv/scl_gethostbyname.c` | R1 inline SWI wrappers (`ctime` bad pointer) |
| `configure.ac`, `doc/UnixLib/Help` | V1 version number, D1 unofficial fork |
| `unix/unix.c`, `signal/post.c`, `sys/_syslib.s`, `incl-local/internal/unix.h`, `vscript` | X2 `_exit` takes an exit code |
| `include/limits.h` | H1 `LLONG_MIN` type |
| `netlib/getserv_r.c` (**new**) | N1 `getservbyname_r` and friends |
| `unix/eventfd.c` | E1 counter updated with thread switching held off |
| `Makefile.am`, `vscript` | B1 build rules and symbol visibility for the above |

The modified files keep their original copyright lines. Code added to them
is marked with a `2026:` comment. The 8 new files carry
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
- An independent review of all the changes (2026-10-01): K5, S6-S9, W2, L5.

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

### W2. `swprintf` / `wcsftime`: Latin-1 formats only (`wchar/wmissing.c`) - commit `817b839`

**Problem.** W1 narrowed the wide format with `(char) format[i]`, which
keeps only the low byte. Any character whose low byte is 0x25 (U+0125,
U+2025, U+FF25 fullwidth E...) became `%` and started a conversion with no
argument behind it, so `swprintf` read arguments that weren't there.

**Evidence.** Found in review. On the PC, the old code gives
`swprintf(buf, 64, L"\xff25d|", 7)` = 2 and `"7|"`: the fullwidth E consumed
the argument as `%d`.

**Change.** A format with a character above 0xFF is refused: `swprintf`
returns -1 with `EILSEQ`, `wcsftime` returns 0. `swprintf` also measures
the output first, so a large `n` (e.g. `INT_MAX` as "no limit") doesn't make
it allocate that much.

**Why this way.** UnixLib's wide functions are Latin-1 only (W1). Passing
other characters through as literal text would need the format split at
each conversion, which `vsnprintf` can't do with one `va_list`.

**Verification.** `tests/host/wchar`, in `make check`: the two functions,
compiled for the PC from the real file, 8 checks. The previous code fails 3.

### W3. `wcsto*`: numbers longer than 127 characters (`wchar/wmissing.c`) - commit `11475df`

**Problem.** Found by the review. W1's `wcstol`, `wcstod` and the rest copy
the number's ASCII characters into a 128-byte buffer and convert that, so a
longer number (leading zeros, a long hex float) was cut at 127 characters
and converted wrongly, with the end pointer in the wrong place.

**Change.** The whole ASCII run is copied: into the stack buffer when it
fits, otherwise into one allocated for the call (falling back to the old
127 characters if that allocation fails).

**Verification.** `tests/host/wchar` converts a 305-digit number and an
overflowing one; both fail on the old code.

### W4. `isw*_l` past the ctype tables (`wchar/wctype_l.c`) - commit `27a2edd`

**Problem.** Found by the review. W1 added a 0-255 check to `iswalpha` and
friends, but the `_l` versions (from GCCSDK) still indexed the 8-bit ctype
tables with any wide character, so W1's claim was only half true.

**Change.** The same check: characters outside 0-255 are "none of the
above", and `towlower_l`/`towupper_l` return them unchanged.

**Verification.** By reading; the code is the same as `wctype.c`'s.

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

### T3. Long sleeps, and sleeping with thread switching held off (`signal/sleep.c`) - commit `9ec19b9`

**Problem.** Found by the review.
- `sleep_int` (upstream) passes centiseconds × 10000 to `ualarm`, a 32-bit
  count of microseconds, which overflows above 429496 centiseconds (about
  71 minutes). The sleep ended early, and T2's `nanosleep` then
  busy-waited for the rest.
- T2's wait (and `sleep_int`'s loop in a Wimp task) called `pthread_yield`,
  which is a fatal error while thread switching is held off.
- `usleep` with 1000000 or more set `EINVAL` but slept anyway (upstream).

**Change.** `sleep` and `nanosleep` sleep in chunks of 400000 centiseconds.
`nanosleep` sleeps again if a sleep ends early without a signal, and only
spins for the last 10-20 ms. `pthread_yield` is only called when thread
switching is allowed. `usleep` returns after `EINVAL`.

**Effect.** Long sleeps last as long as asked. The last 10-20 ms of a
`nanosleep` still spin and can't be interrupted (as in T2).

**Verification.** By reading. Not yet run on RISC OS.

### T4. `CLOCK_MONOTONIC`'s last value shared between threads (`time/clk_gettime.c`) - commit `561fd39`

**Problem.** Found by the review. T1 keeps the last value returned, so the
clock never goes backwards. It is 64 bits, read and written in two
instructions, without a lock: a thread switch in between could leave a
mixed value, which could make the clock jump ahead and stick there.

**Change.** The compare and update are done with thread switching held off
(`__pthread_disable_ints`), when threads are running. Not in the
SharedCLibrary build, which has no threads.

**Verification.** By reading; the SharedCLibrary build compiled by hand.

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

### A2. A heap of several dynamic areas, end to end (`sys/brk.c`, `stdlib/alloc.c`, `sys/_syslib.s`, `incl-local/unistd.h`)

**Problem.** Reported by the OpenTTD port (2026-10-01, Pi 4 with 2 GB,
RISC OS 5).
- RISC OS 5 gives every dynamic area created with `OS_DynamicArea 0` a
  maximum of 128 MB, whatever maximum is asked for. OpenTTD asks for
  512 MB (`__dynamic_da_max_size`) and its `OpenTTD Heap` had a maximum
  of 131072K, as did another UnixLib program's. So a UnixLib heap stopped
  at 128 MB with plenty of memory free.
- When the heap area was full, malloc fell back to `mmap` for the space
  (dlmalloc's "mmap as MORECORE backup", which ignores A1's setting).
  Each such request left an ARMEABISupport `mmap#N` area behind, and the
  allocation still failed.

**What RISC OS 5 allows** (`tests/riscos/daprobe.c`, `HeapProbe` in
UnixLibTests.zip, run on the Pi 4):
- two areas made one after the other land next to each other;
- an area asked for at a given base (R3) is put exactly there, and two
  such areas work as one block (128 KB written across the join);
- physical memory pool areas (as ARMEABISupport uses) are capped at
  128 MB as well, so they don't help.

**Change.**
- The heap is a list of areas. When the area holding the heap's end is
  full, `brk_da` creates the next area directly after it (base = previous
  base + maximum), named after the first (`OpenTTD Heap 2`, `3`..., up to
  64), and carries on. The heap stays one range of addresses, so to malloc
  it is one heap and **a single block can be bigger than 128 MB**.
- All the areas a growth needs are made before any memory is committed,
  so a failure commits nothing for nothing.
- If an area can't be made directly after (the address is taken), the
  heap goes on in an area wherever RISC OS puts it (a new "segment"),
  and adds areas end to end from there. malloc handles that one gap like
  a foreign sbrk (fenceposts around the old top); only the first sbrk of
  a malloc request may start a segment, and that request asks for all its
  space. A block can't cross a gap between segments.
- A new area is asked for with the first area's maximum, at least 128 MB.
  If RISC OS refuses, less is asked for (down to 1 MB). A base where an
  area couldn't be made isn't tried again in that segment.
- The area's size is taken from RISC OS if a growth fails part of the
  way. Empty areas left by a failed growth are removed.
- malloc no longer uses `mmap` as a backup when the heap is in dynamic
  areas. A heap in the wimpslot (no dynamic area) keeps it.
- `__dynamic_area_exit` (program exit and `exec`) removes every area,
  through a new `__dynamic_area_extra_exit`.
- `brk()`/`sbrk()` see the heap as one range within a segment; shrinking
  only gives back memory from the last area. After a segment switch
  `sbrk(0)` is in the new segment and `brk()` into an old one fails.
  `RLIMIT_DATA` still gives the first area's maximum. `fork` is already
  refused with a dynamic-area heap; a `vfork` child shares the list.

**Effect.** A program's heap can use as much memory as is free, and one
allocation can be bigger than 128 MB (OpenTTD's 4096x4096 map needs
128 MB + 16 bytes). Programs whose heap stays in its first area are
unchanged apart from one `OS_DynamicArea 2` the first time the heap
grows. The Task Manager shows the extra areas under the same name with a
number.

**Review.** Both versions were reviewed by two models (Opus and Sonnet)
before release. Fixed from the first review: an `OS_DynamicArea 2` on
nearly every growth, no retry when RISC OS refuses a maximum, a dead
retry in malloc, 32 MB extra areas. Fixed from the second: memory
committed in the old area before a segment switch and then stranded
(now every area is made before committing), repeated attempts at a taken
base, `dalimit` not following a partial growth, address wrap-around near
4 GB.

**Verification.** `tests/emu/heap_test.py` runs the real malloc, free,
realloc and sbrk in the Unicorn emulator, with OS_DynamicArea and
OS_ChangeDynamicArea faked to cap areas at 1 MB and to honour a base in
R3 when the space is free. 37 checks: areas end to end, a 2.5 MB block
across areas, a big block reusing a free top, 400 random calls with
blocks up to 300 KB; the space after the first area taken (a segment
elsewhere, nothing committed for nothing, few SWIs, the other area left
alone); memory running out, also part of the way through a growth; a big
maximum refused; fixed bases refused altogether (the earlier scheme); no
`mmap` SWIs; and the areas removed at exit. At least 8 of these fail on 5.0.3.1-rc3
and 7 on 5.0.3.1-rc2 (which also calls ARMEABISupport's `mmap`).

**On the Pi 4 (2 GB, RISC OS 5, 2026-10-01).** With rc3 (areas wherever
RISC OS put them): `BigHeap` allocated 320 MB in three areas with the
data intact; one 128 MB + 64 KB block was refused; `HeapCheck` passed
(after a fix to the test itself). `HeapProbe` then showed the end-to-end
placement above. The end-to-end heap (rc4) passed: `BigHeap` filled
"UnixLibTest Heap" and "... Heap 2" to their full 131072K and went on in
"... Heap 3" (320 MB in 16 MB blocks, data intact), then got **one 200 MB
block** across the areas, intact. `HeapCheck`: no area left after exit.
`HeapProbe` again placed two areas side by side and one at the base
asked for. OpenTTD 14.1 relinked with rc4 runs with its heap across
several areas and loads a 4096x4096 map (tile array 128 MB + 16 bytes).

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

### X2. `_exit` takes an exit code (`unix/unix.c`, `signal/post.c`, `sys/_syslib.s`, `incl-local/internal/unix.h`, `vscript`)

**Problem.** On the Pi 4 (rc6, 2026-10-01), `ForkOnly`'s child called
`_exit (3)` and its parent's `waitpid` got status 3: "killed by signal
3", not "exited with 3".

**Cause.** In GCCSDK UnixLib, `_exit`'s argument was a wait status (the
`<sys/wait.h>` encoding), because `exit`/`_Exit` and the signal code end
the process through it with an encoded status. POSIX `_exit` takes a
plain exit code, as `exit` does. So any program's `_exit (n)` for n from 1
to 126 ended as if killed by signal n (RISC OS return code 128 + n), and
`_exit (127)` looked like a stopped process. The usual caller is a
fork/vfork child after a failed exec, including UnixLib's own `system()`
and `popen()` (`_exit (EXIT_FAILURE)`).

**Change.** The old function is now the internal `__exit_status`, and
UnixLib's own callers that pass an encoded status (`_Exit`, the signal
code's default actions, the stack-overflow and fatal-error paths in
`_syslib.s`) call it. The public `_exit (status)` calls
`__exit_status (W_EXITCODE (status & 0xff, 0))`. `abort`'s fallback,
`system()` and `popen()` keep calling `_exit` and now get the exit code
they meant.

**What it means for programs.** `_exit (0)` is unchanged. A program
whose fork/vfork child calls `_exit (n)` now sees `WIFEXITED` and
`WEXITSTATUS == n`, as on other systems; Sys$ReturnCode is n.

**Verification.** Disassembly of `_exit` (shift left 8, mask, call
`__exit_status`). On RISC OS: `ForkOnly` and `ForkThreads` check the
fork child's `_exit (3)` with `WEXITSTATUS`; both PASS on a Pi 4 with
5.0.3.1-rc8.

### H1. `LLONG_MIN` has type `long long` (`include/limits.h`)

**Problem.** Reported by the GTK port (2026-10-03): harfbuzz 8.3.0's
`hb_integral_constant<signed long long, LLONG_MIN>` failed to compile
("narrowing conversion of '9223372036854775808' from 'long long unsigned
int'").

**Cause.** `LLONG_MIN` was `0x8000000000000000LL`. A hexadecimal constant
that doesn't fit `long long` has type `unsigned long long`, so `LLONG_MIN`
was a large positive number: in C, `LLONG_MIN < 0` was false and
`x < LLONG_MIN` compared as unsigned, silently.

**Change.** `#define LLONG_MIN (-LLONG_MAX - 1LL)`, as `LONG_LONG_MIN` two
lines above already was and as GCC's own `limits.h` does.

**Verification.** `tests/abi/check.sh` compiles static assertions on the
type, sign and value of the long long limits in C (`-std=c99`) and the
harfbuzz construct in C++; both fail with the old header.

### N1. `getservbyname_r`, `getservbyport_r`, `getservent_r` (new `netlib/getserv_r.c`, `Makefile.am`)

**Problem.** Reported by the GTK port (2026-10-03): `<netdb.h>` declared the
reentrant service lookups but the library didn't define them. GLib 2.80's
meson check only compiles, so it took `getservbyname_r` as present and the
link failed.

**Change.** The three functions call `getservbyname`, `getservbyport` and
`getservent` with thread switching held off (`PTHREAD_UNSAFE`) and copy the
result (name, protocol, alias array and strings) into the caller's buffer,
pointer-aligned. As glibc: 0 with `*result` set; 0 with `*result` NULL if
there's no such service; `ERANGE` if the buffer is too small.
`getservent_r` isn't in the SharedCLibrary build (there's no `getservent`
there).

**Verification.** `tests/host/getserv` (13 checks): the copy, aliases, an
odd buffer address, not found, too small, the exact size and one byte short.

### E1. eventfd's counter is updated with thread switching held off; blocking reads work with threads (`unix/eventfd.c`)

**Problem.** Reported by the GTK port (2026-10-03), from reading the code:
GLib wakes its main loop through an eventfd, written from other threads.

**Cause.** `__eventfd_write` and `__eventfd_read` load the 64-bit counter,
work out the new value and store it, with nothing stopping a thread
switch in between in `write()`, which doesn't hold switching off. A switch
between the load and the store loses the other thread's increment. On ARM
the 64-bit load is two words, so `select()` could also see half an
update. (`read()`, `readv()` and `writev()` hold switching off for the
whole call with `PTHREAD_UNSAFE_CANCELLATION`.)

**Second problem, found on the Pi with 5.0.3.2-rc1.** A blocking read
of a zero counter (`EventFD`) stopped with "EMT - pthread_yield called
with context switching disabled". It waited with `pthread_yield ()`, but
`read()` holds switching off, so a blocking eventfd read never worked with
threads running (the GCCSDK code has the same fault). A blocking
`writev()` would do the same.

**Change.** The load, check and store are done between
`__pthread_disable_ints ()` and `__pthread_enable_ints ()` once threads are
running, as `malloc` does, and released before waiting. `select()` reads
the counter under the hold. To wait, if the caller holds switching off
(the work semaphore isn't 0) that hold is released around `pthread_yield
()`, as `dsp.c` does for its own. The hold's return address
(`__ul_global.pthread_return_address`) is a single global that another
thread's `PTHREAD_UNSAFE` call replaces, so it's saved before the yield and
put back after the hold is taken again. No change to the device handle or
the counter's storage.

**Verification.** `tests/host/eventfd` (35 checks) models the work
semaphore: read, write and select take the hold and release it, it's never
held across `pthread_yield ()`, and none is taken before threads start. A
blocking read and write called as `read()`/`writev()` call them (hold
already taken) release it to yield and get back the same semaphore and
return address after another thread replaced it. Plus the semantics
(semaphore mode, `EAGAIN`, `EINVAL`, a blocking read woken by a write, a
blocking write woken by a read). The first version fails 3 checks; the
original, more. On RISC OS, `EventFD` (`tests/riscos/efdtest.c`) has two
threads write for about 10 seconds while the main thread reads, checks
every write is counted once, then does a blocking read woken by another
thread. With rc1 its blocking read hit the EMT above; with rc2 it
passes on a Pi 4 (2026-10-03). riscos-gtk's GTKTest 0.5 (GLib's eventfd
wake-up with a thread pool and GIO workers) passes glibtest 29/29 on the
Pi with rc2.

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

**Change.** 69 inline SWI wrappers were checked against the PRM (not the
18 in `sound/DRender.h`; see the follow-up below):
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
- Follow-up (5.0.3.1-rc2, after the review): wrappers that gave R0 as an
  input only, although an X SWI returns its error pointer in R0, now give
  it as input and output (`"+r"`): `OS_ReadVarVal`/`OS_SetVarVal` in
  `common/env.c`, the eleven Socket wrappers in `incl-local/sys/socket.h`
  with no other R0 output, and SharedCLibrary `getservbyname`/
  `getservbyport`. The four `sound/DRender.h` calls that pass a sample
  buffer list `"memory"`. The registers the other DigitalRenderer SWIs
  return have not been checked against its documentation.

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
1. `ARMEABISupport_StackOp` 2 says whether the buffer is in a stack, and 3
   gives the stack's bounds. (Until 5.0.3.1-rc1 a buffer below the current
   frame was skipped without asking; that missed a buffer in another
   thread's stack lying below this one, so every buffer is asked about
   now.)
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
page. A zero-length read costs nothing extra.

**Verification.** `tests/emu/swi_test.py` makes the fake OS_GBPB fail if it
writes into a page of a fake stack that no USR-mode access has touched,
and checks:
- a 3-page buffer spanning 4 pages is fully touched before the SWI, and
  only those pages;
- the guard page isn't touched;
- a heap buffer isn't touched, and only StackOp 2 is called for it;
- a buffer in a stack below the current stack pointer (another thread's)
  is touched too;
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

### S6. A fork/vfork child's exit left the parent without sound (`sound/dsp.c`, `sound/midi.c`) - commit `401f52a`

**Problem.** `_exit()` calls `__dsp_exit` and `__midi_exit` in every
process. A fork()/vfork() child has a copy of, or shares, the parent's
variables, so its exit closed the parent's SharedSoundBuffer stream and
MIDISynth connection and stopped its DigitalRenderer session.

**Change.** Each records the process that opened the stream or connection
or activated DigitalRenderer (`getpid()`). In any other process the exit
functions do nothing. If a vfork child was the first to play on the
parent's descriptor, its exit closes its stream, but it leaves the
parent's open count alone. A child's descriptor closes never reach the device:
SUL increments the refcounts at fork.

### S7. A second open of `/dev/dsp` (`sound/dsp.c`) - commit `401f52a`

**Problem.** The device state is per program. A second `open` closed the
first descriptor's stream and reset its settings, and closing the second
set the back end to "none", so the first descriptor's next write went to
DigitalRenderer. SDL's device probing opens and closes the device like this.

**Change.** Further opens share the first one's stream and settings; only
the last close drains and closes the stream. A real OSS device would keep
separate settings per open, or return `EBUSY`. Sharing is the change least
likely to break a program that probes while it plays.

### S8. `SOUND_PCM_READ_*` (`sound/dsp.c`) - commit `401f52a`

**Problem.** Requests are matched on their low 16 bits, and
`SOUND_PCM_READ_RATE`, `READ_CHANNELS` and `READ_BITS` have the same low bits
as `SNDCTL_DSP_SPEED`, `CHANNELS` and `SETFMT`. `READ_RATE` with 0 set the
rate to 4000 Hz. On the old DigitalRenderer path a rate of 0 was ignored,
so this was a regression from S3.

**Change.** They are answered from the full request on both paths, without
changing anything. `<sys/soundcard.h>` encodes requests in one of two ways:
its own `_SIOR`, or `<sys/ioctl.h>`'s `_IOR` if that header was included
first. Both are accepted, here and for `SNDCTL_DSP_GETTRIGGER`.

A first version of this fix also special-cased `SNDCTL_DSP_PROFILE`. In the
second encoding, though, `GETODELAY` has the same value as `PROFILE`, so
that broke `GETODELAY` for such programs. The second review of the fixes
caught it, and the special case was removed.

### S9. DigitalRenderer taken over by another program (`sound/dsp.c`) - commit `401f52a`

**Problem.** S1's "only the program that started it stops it" didn't cover
a takeover. When program B took DigitalRenderer over from A, A still
believed it owned it: A's exit stopped B's sound, and A's next write pushed
samples into B's session.

**Change.** DigitalRenderer doesn't say who is using it, so a program that
activates it puts its pid in the system variable `UnixLib$DSPOwner`. It
only deactivates if the variable still holds its pid. The variable is
removed when the owner deactivates.

A program whose session was taken over, and which writes again while the
new owner is playing, streams into that session, as before. A first
version took the session back on every write, so two programs would fight
over it; the second review caught that. Once the other program has
stopped, the next write starts and claims a new session.

A program that doesn't set the variable can't be detected: one built with
an older UnixLib, or one not using UnixLib. After it takes over, the
variable still holds the first program's pid, and the first program stops
DigitalRenderer when it closes or exits, as it always did.

**Verification of S6-S9.** Host tests, with fakes for `getpid` and system
variables:
- dsp: 156 checks;
- midi: 24 checks.

They now use UnixLib's own `<sys/soundcard.h>`, so the RISC OS request
numbers are tested. Before, they picked up the PC's header.

Against the previous `dsp.c`:
- the second-open test hangs (the first descriptor ends up waiting on
  DigitalRenderer);
- 9 of the other new checks fail: `READ_RATE` gives 4000, the child's exit
  closes the stream, and the takeover cases.

Against the previous `midi.c`, the child's exit closes the parent's
connection. Not yet run on RISC OS.

---

### S10. `/dev/dsp` fragments, `GETOPTR` after `RESET`, the conversion buffer (`sound/dsp.c`) - commit `53e4add`

**Problem.** Found by the review.
- `SNDCTL_DSP_SETFRAGMENT` shifted 1 by the requested exponent before
  limiting it (undefined above 31), and a fragment could be bigger than
  the 2 s queue limit, so `GETOSPACE` reported 0 fragments for ever.
- After `SNDCTL_DSP_RESET`, `GETOPTR` worked from the closed stream's
  counters and returned a huge block count.
- The 8 KB conversion buffer was static, so every program had it.

**Change.** The exponent is limited to 7-16 before the shift; a fragment
is at most half the queue limit. Closing the stream resets the `GETOPTR`
counters. The buffer is allocated on the first SharedSoundBuffer write
(`ENOMEM` if that fails).

**Verification.** `tests/host/dsp`: 2 x 64 KB fragments at 8 kHz mono, an
exponent of 40, and `GETOPTR` after `RESET`; they fail on the old code.

**Not changed (documented in SOUND.md):** less than a fragment written
waits for more, `POST`, `SYNC` or `close()` before playing; the module
versions aren't checked.

### S11. `/dev/midi` when MIDISynth is full; closing MIDI hardware (`sound/midi.c`) - commit `53e4add`

**Problem.** Found by the review. When MIDISynth took nothing, `write`
returned 0, which a write-all loop retries for ever. Closing `/dev/midi`
on MIDI hardware sent "all notes off" and "reset controllers" to all 16
channels even if the program never sent anything.

**Change.** Nothing written and the module full: `O_NONBLOCK` writes fail
with `EAGAIN`; blocking ones wait for room (yielding to other threads when
that's allowed) for up to 2 s, then fail with `EIO`. Some bytes written is
still a short write. The hardware reset on close only happens if the
program sent something.

**Verification.** `tests/host/midi`: the three full-module cases, no
`pthread_yield` with thread switching held off, and a close without
writing; they fail on the old code. `MIDI_TxByte`'s "buffer full" return
is still not checked (to be checked against the MIDI module's
documentation).

## 4. Thread ticker (K1-K9)

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
- Since run on RISC OS (Pi 4, 2026-10-01): without the module (the RMA
  copy, `via=RMA` lines from the Ticker tests with 5.0.3.1-rc9), and
  refusing `*RMKill` while in use (`ForkThreads`).

### K5. A fork/vfork child's exit freed the parent's ticker block (`pthread/pthinit.c`, `pthread/ticker.c`) - commit `0096397`

**Problem.** `_exit()` calls `__pthread_prog_fini` in every process. A
fork()/vfork() child has a copy of, or shares, the parent's pointer to the
pthread RMA block and its PThreadTicker attachment. A child that exited
(typically `_exit(127)` after a failed `exec`) freed the parent's block
and detached the parent from the module. The parent then restarted its
ticker on the freed block (`__fork_post`). The free itself was an upstream
bug. Since K1/K4, though, the block holds running code (the RMA copy) and
counters the handler writes every 2 cs, and the module could be killed
while the parent still used it.

**Evidence.** Found in review, by reading the code path. Not seen on a
machine.

**Change.**
- `__pthread_ticker_init` records the owning process (`getpid()`, SUL's
  pid, unique while the process exists), and `__pthread_ticker_owner()`
  says whether this is it.
- In a child, `__pthread_prog_fini` only stops a ticker the child may have
  started, then returns without writing stats, detaching or freeing the
  block.
- `__pthread_ticker_fini` and the stats writer refuse in a child too.
- If `__pthread_ticker_init` never ran (a fatal error earlier in
  `__pthread_prog_init`), the process counts as the owner, so its block is
  still freed, as before.

**Verification.** `tests/host/ticker` (38 checks): a child's fini doesn't
detach, and the parent's does. `__pthread_prog_fini` itself isn't in a
host test, because it needs the whole start-up. Not yet run on RISC OS.

---

### K6. `exec` detaches from PThreadTicker; `busy` is a count (`sys/exec.c`, `pthread/ticker.c`) - commit `2ef1bec`

**Problem.** Found by the review. `exec` stops the ticker but never
detached from the module, so the program stayed counted and the module
couldn't be killed until a reboot (the safe direction). Separately, the
`busy` flag that keeps the re-check out of start/stop was set and cleared,
so a nested call would clear it for the outer one.

**Change.** `exec` calls `__pthread_ticker_fini` after stopping the ticker
(it does nothing in a fork/vfork child, whose parent still uses the
module). `busy` is incremented and decremented.

**Verification.** `tests/host/ticker` still passes. Not yet run on RISC OS
(a threaded program that `exec`s, then `*RMKill PThreadTicker`).

### K7. PThreadTicker 0.02: the count updated with IRQs off (`module/pthticker.s`) - commit `f32a3e0`

**Problem.** Found by the review. attach/detach load, change and store the
count. Two programs in TaskWindows (which are switched pre-emptively)
starting or quitting at the same moment could interleave and lose a count;
too low a count would let the module be killed while in use.

**Change.** The update is done between `OS_IntOff` and `OS_IntOn`, skipped
if IRQs are already off; registers and flags are preserved as before. The
interface is unchanged, so every UnixLib that uses the module works with
0.02, and 0.02 is still loaded with `RMEnsure PThreadTicker 0.01` (a
newer `RMEnsure` would try to replace a copy in use and stop the `!Run`
file).

**Verification.** `tests/emu/ticker_test.py` checks the SWIs, the count,
and that flags and the I bit are restored, with IRQs on and off.

### K8. fork() on EABI: the child's exit and the parent's stack (`sys/vfork.c`, `sys/_vfork.s`)

**Problem.** On the Pi 4 (2026-10-01), `ForkOnly` (fork and `_exit`, no
threads) and `ForkThreads` aborted ("abort on data transfer at
&206CDC14", in the RMA) as soon as the fork child exited. `ForkExec`
(vfork + exec, `system()`) passes.

**Cause.** On EABI the program's stack is an ARMEABISupport stack, outside
application space. SharedUnixLibrary records its handle in the process
structure and sets the "ARMEABI" status flag; when such a process exits,
it asks ARMEABISupport to free that stack. sul_fork copies the whole
structure, flag and handle included, to the child, and the child runs on
its parent's stack. Two things followed:

1. A child that exits through UnixLib (`_exit`/`exit` after fork, or
   after a vfork whose exec failed) had SharedUnixLibrary free the
   parent's stack. In rc5 the child's handle was set to 0 instead, but
   ARMEABISupport's free doesn't check for 0 (GCCSDK
   `armeabisupport/stack.c`, `stack_free`), so it aborted there: the
   abort above. A child that execs a command is not affected: when the
   command ends, SharedUnixLibrary replaces the status word (flags
   included) with the return code.
2. SharedUnixLibrary's copy of the parent for fork covers application
   space only, so not the stack. A child that returns from the function
   that called fork and then uses the stack overwrites the parent's
   frames there.

This is in GCCSDK UnixLib as it is (SharedUnixLibrary is unchanged here).
The first sighting was the rc4 `ForkExec` test (which then also forked):
"abort on data transfer at &14AF0", the first store to the stack in
`fork_common` after the parent resumed, on the stack its fork child's
exit had freed.

**Change.** EABI builds only.
- In the child, `__fork_post` clears the ARMEABI flag in the child's
  process structure, so SharedUnixLibrary frees no stack when the child
  exits. A UnixLib program the child execs sets the flag again with its
  own stack. (rc5's handle of 0 is gone.)
- `fork` passes its entry `sp` to `__fork_pre`, which copies the stack
  from there to the top (bounds from `ARMEABISupport_StackOp` 2 and 3)
  into the heap, which the fork copy includes. In the parent,
  `__fork_post` copies it back; its own frame is below that part. If the
  copy can't be allocated, fork fails with ENOMEM. vfork doesn't copy
  anything, as before (a vfork child must not return from its caller).

**Verification.** Disassembly: the child clears bit 0 of byte 99 of the
process structure (status word at 96, flag bit 24). On RISC OS:
`ForkOnly` now also has a child return from the function that called fork
and use 8 KB of stack over its frame; the parent checks a pattern there.
Pi 4, 5.0.3.1-rc8 (with X2): `ForkOnly` PASS (three forks and the stack
check), `ForkThreads` PASS (with PThreadTicker loaded: RMKill refused),
`ForkExec` PASS (without the module).

### K9. Threads starved in a program that polls often (`incl-local/internal/ticker.s`, `module/pthticker.s` 0.03, `pthread/ticker.c`, the RMA block)

**Problem.** `Ticker` on a Pi 4 (2026-10-01, rc5-rc8): "598959 polls,
threads counted 0 and 0". A Wimp program whose main thread polls more
often than every 2 cs (SDL programs do) gave its other threads no
processor time at all.

**Cause.** The pre-filter stopped the ticker as the program entered
`Wimp_Poll` and the post-filter started it again as it returned. Each
`OS_CallEvery` began a new 2 cs period, and the next pre-filter removed it
before the period was up, so the ticker never fired while the program
was running. GCCSDK UnixLib's filters did the same.

**Change.** The ticker keeps running (details: docs/THREAD-TICKER.md,
"Starved threads"):
- the pre-filter sets `polling` in the RMA block, the post-filter clears
  it; both first check that the program is paged in (as the handler
  does), since FilterManager can call them for another task;
- a tick while `polling` is set sets `pending` instead of a callback (a
  callback then could be taken by whichever task `Wimp_Poll` returns to);
- the post-filter, if `pending` was set, sets the callback, so threads are
  switched as `Wimp_Poll` returns (counted in `post_switches`, shown in
  `UnixLib$TickerStats`);
- start and stop clear `polling`.

The RMA block grew from 472 to 640 bytes (three fields at 152-163, the
routines' copy, now 476 bytes, the whole room, at 164; `tools/check-lib.sh` expects 640).
PThreadTicker 0.03 has the new routines and interface version 2. UnixLib
uses the module only if it is version 2; earlier UnixLib uses only version
1, so neither uses a module with the other's routines or block layout,
and falls back to its own RMA copy.

**What it means for programs.** Threads in a Wimp program that polls fast
now run: each tick that comes while the program is in `Wimp_Poll` gives a
thread switch as `Wimp_Poll` returns, so they get time at the 2 cs rate
as before the filters, without a callback ever landing in another task.
Programs that poll slowly see no difference. The ticker now also runs
while other tasks do; its handler only counts then.

**Review.** Before release, a review found that the first version's
"is the program paged in?" subroutine returned through lr after a SWI,
which in SVC mode overwrites lr: every filter call (and the handler, in
SVC mode) would have looped for ever, i.e. the first `Wimp_Poll` of a
threaded program would have hung the machine. The emulator's fake SWIs
didn't touch lr, so the test missed it. Fixed (the subroutine saves lr),
and the emulator now overwrites lr at every SWI and checks that each call
returns; with the first version it reports 76 failures. The review also
noted a few instructions in the pre-filter where a tick could still set a
callback (as in the old design); IRQs are now off there.

**Verification.** `tests/emu/ticker_test.py` (210 checks, both the
module's and the RMA copy's machine code): ticks while polling set
`pending` and no callback; the post-filter switches once and clears both;
filters called for another task change nothing; critical sections are
respected; R0-R3 and flags preserved. `tests/host/ticker`: a version 1
module isn't used. On RISC OS (Pi 4, 5.0.3.1-rc9, two runs): `Ticker`
PASS, "1055743 polls, threads counted 1493779377 and 1486347011" and
"1023330 polls, ... 1477175375 and 1474375386", against "598959 polls,
threads counted 0 and 0" before. The main thread still polled about
50000 times a second, and no other program crashed. `TickerStartTask`
PASS too: "803771 polls, 3 child tasks, threads counted 1197408353 and
1197078111". With PThreadTicker 0.03 loaded (LoadTicker):
`Ticker` PASS ("1065393 polls, threads counted 1520222480 and
1510499546"), and `ForkThreads` found the module with 1 program
attached (so a 5.0.3.1 program uses interface version 2), RMKill
refused, PASS. `ExitJoin` PASS. `TickerEarly` (threads started before
`Wimp_Initialise`) PASS: "1059940 polls, threads counted 1525181594 and
1522396949". The TickerStats lines from these runs: `ticks` about
1000 in 20 s (the ticker ran its full 2 cs rate throughout), `pre` =
`post` = the poll count, `post_switches` 192-233 (about a fifth of the
ticks came while the program was in `Wimp_Poll` and switched threads as
it returned), `foreign` 54-228 (ticks while other tasks ran, now expected
and harmless; nothing crashed), `via=RMA` and, after LoadTicker,
`via=module`. (The program name in those lines read "BASIC": it is taken
from `OS_GetEnv` at exit, which by then can be another task's command
line. Cosmetic.)

## 5. Files over 2GB (L1-L5)

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
it maps to the new `mmap64`, which takes a 64-bit offset. On EABI (since
5.0.3.1-rc2) offsets up to 4GB-1, the largest RISC OS file, are mapped:
ARMEABISupport keeps the offset as one 32-bit word, which `mmap`, `munmap`
and `msync` now treat as unsigned, moving the file position with `lseek64`.
Larger offsets give `EOVERFLOW`. `mmap` itself refuses a negative offset
(`EINVAL`, as POSIX says); it used to seek nowhere and map whatever
followed the file's current position. Other builds: the offset must fit in
32 bits signed.

**Effect.** New compiles are correct. Objects already compiled with
`_FILE_OFFSET_BITS=64` that call `mmap` still pass the offset wrongly, as
before, until they are recompiled.

### L5. `glob()` with `GLOB_ALTDIRFUNC` under `_FILE_OFFSET_BITS=64` (`unix/glob.c`) - commit `5da8c32`

**Problem.** New with L1. A program built with `_FILE_OFFSET_BITS=64`
has a 72-byte `struct stat`. With `GLOB_ALTDIRFUNC` it gives `glob()` its own
`gl_stat`/`gl_lstat`, which fill that layout. `glob.c` is built without
large files, though, and passed a 64-byte `struct stat` on its stack, so 8
bytes were overrun. GNU make does this.

**Change.** `glob2` passes a buffer big enough for either layout (a union of
`struct stat` and `struct stat64`). It only reads `st_mode`, which comes
before `st_size` and so is at the same offset in both.

**Verification.** `glob2`'s stack frame grows from 92 to 100 bytes (from
the disassembly). No runtime test.

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

### V1. Version number (`configure.ac`, `doc/UnixLib/Help`) - commits `d0bff5c`, `de2f01e`, `f9a1d48`, and the 5.0.3, 5.0.3.1 and 5.0.3.2 release commits

`AC_INIT` and the Help file say **5.0.3.2**. Releases continue UnixLib's own
numbering from GCCSDK's 5.0. The libtool `-version-info` is `7:0:2`:
interfaces have been added (6:0:1 for 5.0.3.1; `getservbyname_r` and
friends for 5.0.3.2) and none removed, so the shared library's major
version stays 5.

### D1. Unofficial fork (`doc/UnixLib/Help`, `configure.ac`; README and other docs)

The Help file used to say "The UnixLib is part of the RISC OS GCCSDK … This
should be checked for updates and bug-fixes", and `configure.ac`'s
bug-report address was GCCSDK's. Both now say that this is riscos-unixlib,
an unofficial fork, not released or supported by the GCCSDK developers.
They send reports to this repo, and the Help file still points to GCCSDK
for the official UnixLib. README, this file, MAINTAINING and the
PThreadTicker ReadMe say the same.

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
- a threaded program without PThreadTicker;
- R1 and R2 (on the machine where Warzone 2100 crashed).
- S6-S11, W2-W4, T3, T4, L5 and the L4/R1/R2 follow-ups (the fixes
  from the 2026-10-01 review).

**Done on RISC OS (Pi 4, 5.0.3.1-rc8 and rc9; released as 5.0.3.1 with
the sound tests still to run):** `ForkOnly`, `ForkThreads`,
`ForkExec` (K5, K6, K8, X2), `ExitJoin` (X1), `FsyncRO` (F1), `Sched`
(P1, P2), `BigHeap`/`HeapCheck` (A2) all PASS.

**Also:** `*RMKill PThreadTicker` while a UnixLib program was
using the module (Pi 4, 2026-10-01) was refused with "PThreadTicker is in
use by UnixLib programs". This settles the point the review disputed:
`OS_Module 18` gives the private word's contents (the workspace pointer)
in R4, as the code assumes, so the module's count works.

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
