# The thread ticker

How UnixLib switches threads on RISC OS, what went wrong with it in a
multitasking desktop, what changed in 2026, and how to read the
`UnixLib$TickerStats` counters.

## How it works

RISC OS has no pre-emptive threads, so UnixLib makes its own. When a
program has more than one thread, UnixLib starts an **OS_CallEvery ticker**
(every 2 cs). Its handler sets an OS callback; when RISC OS next returns
to the program in USR mode, the callback handler (`__pthread_callback` in
`pthread/_context.s`) saves the running thread's registers and the
scheduler (`__pthread_context_switch` in `pthread/context.c`) picks the next
one.

A Wimp task shares the machine with other tasks. When it calls
`Wimp_Poll`, other tasks run and its memory is paged out. So UnixLib
registers two **Wimp filters** (FilterManager) for the program's task:

- the **pre-filter** runs as the program enters `Wimp_Poll` and notes
  that it is polling (`polling` in the RMA block);
- the **post-filter** runs as `Wimp_Poll` returns to the program, clears
  that, and switches threads at once if a tick came while it was polling
  (`pending`).

The ticker keeps running all the time. A tick while the program is in
`Wimp_Poll` sets no callback (whichever task `Wimp_Poll` returns to would
take it); it only sets `pending`.

Until 5.0.3.1 the pre-filter stopped the ticker and the post-filter started
it again. Each start began a new 2 cs period, so a program that called
`Wimp_Poll` more often than every 2 cs, as SDL programs do, never got a
tick at all: its other threads (audio, loading) got no processor time
while it sat in its event loop. The Pi test `Ticker` polls about 30000
times a second; with 5.0.3.1-rc8 both its threads were counted 0 after
20 s. See "Starved threads" below.

The ticker is a system-wide interrupt: it fires whichever task is paged
in. The handler therefore first checks, with `OS_ChangeEnvironment 16`,
that the current upcall handler is SharedUnixLibrary's and that its R12
is this program's key. Only then does it look at the program's memory.

All the state the handler needs is in the **pthread RMA block**
(`struct __pthread_callevery_block` in `incl-local/pthread.h`, the
`PTHREAD_CALLEVERY_RMA_*` offsets in `incl-local/internal/asm_dec.s`),
claimed by `sys/_syslib.s` at start-up.

## What went wrong

Warzone 2100 on a Pi 4: other tasks (Organizer, Alarm) died with "Internal
error: abort on instruction fetch" while Warzone was running. The ticker
was firing while another task was paged in, and its handler lived in the
program's memory, so the address pointed at whatever the other task had
there. The upcall check never got a chance: the handler's own first
instruction wasn't there.

That shouldn't happen if the filters work, so there are two questions:
where the ticker code lives, and why the filters didn't stop it.

### Where the code lives (fixed)

1. **First fix (in 0.1.1-rc1):** the handler was copied into the RMA block and run
   from there. It works, but running code out of a data block is fragile
   (it assumes RMA is executable, the copy had to fit a fixed space, and a
   stale object once made the copy overrun the block).
2. **Now:** a small module, **PThreadTicker** (`module/pthticker.s`),
   holds the ticker routines, and UnixLib runs them from there when it's
   loaded. A module is always paged in. Without the module, UnixLib still
   copies its own routines into the RMA block and runs the copy, so
   programs work either way. (For a day the routines were in a
   SharedUnixLibrary "1.17"; that was withdrawn: SUL belongs to GCCSDK,
   and an unofficial SUL under the official name wasn't a good idea.)

Why keep a module when the RMA copy works: RISC OS Open has discussed
splitting the RMA so that modules' code and data claimed with
`OS_Module 6` live apart (forum thread "Split RMA"), which would let the
data part be made non-executable. The RMA copy would then stop working;
code in a loaded module would not. The module is the long-term route, the
RMA copy the fallback until it's widely installed.

The **filters** were in the program's memory too. FilterManager only calls
them for the task they were registered for, which is safe if that task is
really this program. Now they are in the module (or the RMA copy) as well.

### Why the filters didn't stop the ticker

Candidates, and what 2026 did about each:

| | Suspect | Change |
|---|---|---|
| H1 | **Wrong task handle.** `_syslib.s` reads `Wimp_ReadSysInfo 5` at start-up, before `Wimp_Initialise`, and `__pthread_start_ticker` only re-read it while it was 0. If the value before `Wimp_Initialise` isn't the program's final handle (the parent's, say), the filters were registered for another task: that task's polls stopped and restarted our ticker, ours didn't. | The handle is read afresh whenever the ticker starts, and every ~1.3 s while it runs; the filters move if it changed. They're always removed with the handle they were registered with. |
| H2 | **Threads started before `Wimp_Initialise`** (SDL's timer or audio thread, created before the window). The handle was 0, so the ticker started with **no** filters, and they were only registered at the next `pthread_create`, which may never come. | The periodic check registers them once the program is a task. |
| H3 | **Task switches that don't go through the program's `Wimp_Poll`,** e.g. its own `Wimp_StartTask` (Warzone starts URIdispatch for web links): the child runs while we're paged out, with no pre-filter call. Also possible: TaskWindow paging, `Wimp_TransferBlock`. | Can't be prevented by filters. Harmless now: the handler is always there, sees another task's upcall handler and only counts. |
| H4 | **Filter bookkeeping** in a static flag in the program. | Replaced by the handle the filters are registered for (in `pthread/ticker.c`). |

**Confirmed on a Pi (2026-09-28, Warzone 2100 with 0.1.1-rc1 and the
module, 17 minutes with other tasks running, no crashes):**

```
warzone2100 ticks=50980 foreign=1 ... pre=9307 post=9315
 filters_for=0x35803a20 startup=0/.. cached=0x35803a20 now=0/..
 first_start=0 starts=4 filter_moves=1 filter_errors=0 post_switches=380 via=module
```

- **H2 was the cause.** `first_start=0`: the ticker first started with no
  filters (SDL's threads exist before `Wimp_Initialise`). The old library
  would never have registered them; now the periodic check did
  (`filter_moves=1`), and the filters then ran thousands of times.
- **H1 is ruled out:** `startup=0`, i.e. `Wimp_ReadSysInfo 5` returns 0
  before `Wimp_Initialise`, not another task's handle.
- **H3 happens but rarely:** 1 tick in 50980 found another task paged in.
  Harmless now.

## Starved threads (fixed in 5.0.3.1)

**Seen:** `Ticker` on a Pi 4 (2026-10-01, 5.0.3.1-rc5 to rc8): "PASS:
598959 polls, threads counted 0 and 0". The test's main thread polls with
null events, so `Wimp_Poll` comes back within microseconds while no other
task wants the processor.

**Cause:** each post-filter restarted the ticker with `OS_CallEvery`, and
the next pre-filter (microseconds later) removed it before its first 2 cs
were up. In GCCSDK UnixLib the same filters did the same, so this is not
new; it hits any Wimp program with threads that polls quickly.

**Change:** the filters no longer stop and start the ticker
(`incl-local/internal/ticker.s`):

- pre-filter: if the program is paged in (the same upcall check as the
  handler; FilterManager can call it for another task, H1), set
  `polling`;
- handler: as before, but if `polling` is set it sets `pending` instead of
  a callback (also when it finds another task paged in);
- post-filter: if the program is paged in, clear `polling`; if `pending`
  was set, clear it and set the callback (unless in a context switch or
  a critical section), so the thread switch happens as `Wimp_Poll`
  returns; count it in `post_switches`. It also starts the ticker if it
  isn't running, as before;
- start and stop clear `polling` (and stop `pending`).

So a program that polls fast gets a thread switch on the first return
from `Wimp_Poll` after each 2 cs tick, and one that is paged out for a
while gets one as soon as it is paged back in.

The ticker now runs while other tasks run. That was already the case with
H2/H3 and is harmless: the handler only counts (`foreign`) when another
task is paged in. The cost is one `OS_ChangeEnvironment` every 2 cs.

The routines grew from 256 to 464 bytes; the RMA block grew from 472 to
640 bytes (`polling`, `pending`, `post_switches` at 152-163, the copy of
the routines at 164). Module **0.03** has the new routines and interface
version **2**. UnixLib 5.0.3.1 uses the module only if it is version 2,
else its own copy; earlier UnixLib uses only version 1, so it ignores
0.03 and uses its copy (with the old behaviour).

## `UnixLib$TickerStats`

Set the variable to a file name and every UnixLib program that exits
appends one line to it (nothing is written if it's unset):

```
*Set UnixLib$TickerStats <Wimp$ScrapDir>.TickerStats
```

Example (one line):

```
tickertest ticks=1003 foreign=12 last_foreign=0x2211a8/0x0 pre=412 post=411
 filters_for=0x4d0c1234 startup=0x4d0c1234/310 cached=0x4d0c1234
 now=0x4d0c1234/310 first_start=0x4d0c1234 starts=1 filter_moves=1
 filter_errors=0 post_switches=380 via=module
```

| Field | Meaning |
|---|---|
| (first word) | the program: the leaf name of the command line's first word |
| `ticks` | ticker calls |
| `foreign` | ticker calls that found **another task** paged in. Non-zero = the ticker was live while we were paged out. |
| `last_foreign` | the upcall handler / its R12 at the last foreign tick. SharedUnixLibrary's handler with another key = another UnixLib program (e.g. a child); anything else = a non-UnixLib task. |
| `pre`, `post` | Wimp pre-/post-filter calls. Roughly one each per `Wimp_Poll`. |
| `filters_for` | the task handle the filters were last registered for |
| `startup` | `Wimp_ReadSysInfo 5` at start-up: handle / Wimp version (R1) |
| `cached` | `__ul_global.taskhandle` at exit |
| `now` | `Wimp_ReadSysInfo 5` at exit: handle / Wimp version |
| `first_start` | the handle the filters were registered for when the ticker first started (0 = none: H2) |
| `starts` | times the ticker was started |
| `filter_moves`, `filter_errors` | filter registrations that worked / failed |
| `post_switches` | thread switches set by the post-filter because a tick came while the program was in `Wimp_Poll` (5.0.3.1) |
| `via` | `module` (PThreadTicker's routines) or `RMA` (the copy) |

Reading it:

- `pre` = 0 and `foreign` > 0: the filters never ran for our polls (H1 or
  H2). `first_start` = 0 is H2; `startup` ≠ `now` with `filters_for` =
  `startup` is H1 (only possible with the old library).
- `pre` ≈ `post` ≈ number of polls, `foreign` small: the filters work and
  the foreign ticks come from paging outside `Wimp_Poll` (H3), e.g. during
  `Wimp_StartTask`.
- `foreign` = 0: the ticker was never live in another task.

The Pi tests `Ticker`, `TickerEarly` (threads before `Wimp_Initialise`) and
`TickerStartTask` (a `Wimp_StartTask` child every 5 s) set the variable to
`<Wimp$ScrapDir>.TickerStats`.

## The PThreadTicker module

Title `PThreadTicker`, file `PThrTicker` (type Module), built
with the library (`build/work/build/pthticker`). No SWIs, commands or
service calls, so it needs nothing allocated by anyone.

UnixLib finds it at start-up with `OS_Module 18` ("PThreadTicker"), which
gives the module's address and its workspace, and reads the interface
table that follows the module header:

| Offset | Contents |
|---|---|
| &34 | `"PTTk"` (&6B545450) |
| &38 | interface version: 2 (0.03); 1 in 0.01 and 0.02 |
| &3C | number of entries that follow: 7 |
| &40… | offsets from the module start of: handler, start, stop, pre-filter, post-filter (called with R12 = the program's RMA block); attach, detach (R12 = the module's workspace) |

If the module isn't there, or the magic or version doesn't match, UnixLib
uses its RMA copy. UnixLib 5.0.3.1 wants version 2; earlier versions want
1. So a program and a module from different sides of that change don't
use each other, and the program runs its own copy, which works the same.

- **attach/detach:** UnixLib attaches at start-up and detaches at exit,
  after stopping its ticker and removing its filters. The module refuses
  to be killed while anything is attached ("PThreadTicker is in use by
  UnixLib programs"): its code would still be called. A program that dies
  without reaching `_exit` stays counted, so the module can't be killed
  until the next restart (its ticker, if left running, is harmless).
  `exec` detaches too (since 5.0.3.1-rc2; before, a program that
  `exec`'d stayed counted). Since module 0.02 the count is updated with
  IRQs off (`OS_IntOff`), so two programs in TaskWindows can't lose a
  count by updating it at once.
- **Loading:** a program uses the module only if it was loaded before the
  program started. Put `PThrTicker` in `!System.310.Modules` (or in the
  application) and load it from `!Run`:
  `RMEnsure PThreadTicker 0.01 RMLoad System:Modules.PThrTicker`. It is
  optional: without it programs still work. (0.01 rather than the latest
  version on purpose: a newer RMLoad would try to replace a copy in use,
  which refuses, and stop the `!Run` file.)
- **Interface:** the routines read these offsets in the RMA block, and the
  module and UnixLib are released separately, so they must never move: 76
  upcall handler, 80 its R12 (the SUL key), 88 ticker started, 92
  thread-switch semaphore, 96 callback semaphore, 120–143 the counters,
  and from version 2: 152 polling, 156 pending, 160 post_switches.
  New fields go after 163, before the copy of the routines. A change to the routines' interface means a new
  interface version at &38.
- **Name:** "PThreadTicker" isn't registered with RISC OS Open. Register
  it (a free allocation, not a code submission) before a wide release.

## Where the code is

| File | What |
|---|---|
| `incl-local/internal/ticker.s` | the routines (handler, start, stop, two filters) as one macro, used by the module and UnixLib |
| `module/pthticker.s` | the PThreadTicker module: header, interface table, workspace, attach/detach, `TICKER_ROUTINES pt` |
| `pthread/_context.s` | UnixLib's copy (`__pthread_call_every_code` … `_end`), offsets, `__pthread_ticker_call` |
| `pthread/ticker.c` | finding the module, start/stop, the filters, the periodic check, stats |
| `pthread/context.c` | calls `__pthread_ticker_recheck` every 64 switches |
| `pthread/pthinit.c` | `__pthread_ticker_init` at start-up; stats, stop and release at exit |
| `tests/host/ticker`, `tests/emu/ticker_test.py` | host test (C, fake SWIs) and emulator test (the built machine code) |
