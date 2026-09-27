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

- the **pre-filter** runs as the program enters `Wimp_Poll` and stops the
  ticker;
- the **post-filter** runs as `Wimp_Poll` returns to the program and starts
  it again.

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

1. **0.1.1 (first fix):** the handler was copied into the RMA block and run
   from there. It works, but running code out of a data block is fragile
   (it assumes RMA is executable, the copy had to fit a fixed space, and a
   stale object once made the copy overrun the block).
2. **Now:** **SharedUnixLibrary 1.17** has the ticker routines itself and
   runs them for the program (`SharedUnixLibrary_Ticker`, below). A module
   is always paged in. With SUL 1.16 or older, UnixLib still copies its own
   routines into the RMA block and runs the copy, so programs keep working
   with the old module.

The **filters** were in the program's memory too. FilterManager only calls
them for the task they were registered for, which is safe if that task is
really this program. Now they are in SUL (or the RMA copy) as well.

### Why the filters didn't stop the ticker

Candidates, and what 2026 did about each:

| | Suspect | Change |
|---|---|---|
| H1 | **Wrong task handle.** `_syslib.s` reads `Wimp_ReadSysInfo 5` at start-up, before `Wimp_Initialise`, and `__pthread_start_ticker` only re-read it while it was 0. If the value before `Wimp_Initialise` isn't the program's final handle (the parent's, say), the filters were registered for another task: that task's polls stopped and restarted our ticker, ours didn't. | The handle is read afresh whenever the ticker starts, and every ~1.3 s while it runs; the filters move if it changed. They're always removed with the handle they were registered with. |
| H2 | **Threads started before `Wimp_Initialise`** (SDL's timer or audio thread, created before the window). The handle was 0, so the ticker started with **no** filters, and they were only registered at the next `pthread_create`, which may never come. | The periodic check registers them once the program is a task. |
| H3 | **Task switches that don't go through the program's `Wimp_Poll`,** e.g. its own `Wimp_StartTask` (Warzone starts URIdispatch for web links): the child runs while we're paged out, with no pre-filter call. Also possible: TaskWindow paging, `Wimp_TransferBlock`. | Can't be prevented by filters. Harmless now: the handler is always there, sees another task's upcall handler and only counts. |
| H4 | **Filter bookkeeping** in a static flag in the program. | Replaced by the handle the filters are registered for (in `pthread/ticker.c`). |

The counters below say which of these happens on a real machine. Until a
Pi run says otherwise, H2 is the likeliest for SDL programs.

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
 filter_errors=0 via=SUL
```

| Field | Meaning |
|---|---|
| (first word) | the program (command line's first word) |
| `ticks` | ticker calls |
| `foreign` | ticker calls that found **another task** paged in. Non-zero = the ticker was live while we were paged out. |
| `last_foreign` | the upcall handler / its R12 at the last foreign tick. SUL's handler with another key = another UnixLib program (e.g. a child); anything else = a non-UnixLib task. |
| `pre`, `post` | Wimp pre-/post-filter calls. Roughly one each per `Wimp_Poll`. |
| `filters_for` | the task handle the filters were last registered for |
| `startup` | `Wimp_ReadSysInfo 5` at start-up: handle / Wimp version (R1) |
| `cached` | `__ul_global.taskhandle` at exit |
| `now` | `Wimp_ReadSysInfo 5` at exit: handle / Wimp version |
| `first_start` | the handle the filters were registered for when the ticker first started (0 = none: H2) |
| `starts` | times the ticker was started |
| `filter_moves`, `filter_errors` | filter registrations that worked / failed |
| `via` | `SUL` (SharedUnixLibrary 1.17 runs the ticker) or `RMA` (the copy) |

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

## `SharedUnixLibrary_Ticker` (SWI &55C85, SUL 1.17)

| R0 | Action | Other registers |
|---|---|---|
| 0 | start the ticker for the program | R1 = key, R2 = the pthread RMA block |
| 1 | stop it (the block stays registered) | R1 = key |
| 2 | stop it and forget the block (call before freeing the block) | R1 = key |
| 3 | read the filter routines | R1 = key; on exit R1 = pre-filter, R2 = post-filter (register them with R2 = the block) |

The key is the one `SharedUnixLibrary_Initialise` returned (the process
structure; UnixLib keeps it in the RMA block as `sul_upcall_r12`).
Unknown key: error `&81A401`; unknown reason: `&81A400`. If the program
exits without stopping the ticker, SUL's exit handler removes it.

UnixLib calls reason 3 at start-up; if that fails (SUL 1.16 or older) it
uses its RMA copy. It asks for the SWI, not the version, so a SUL without
the SWI always gets the fallback.

**Interface:** the routines read these offsets in the RMA block, and SUL
and UnixLib are released separately, so they must never move: 76 upcall
handler, 80 its R12 (the key), 88 ticker started, 92 thread-switch
semaphore, 96 callback semaphore, 120–143 the counters. New fields go
after 148.

### Installing SUL 1.17

The module is built with the library (`build/work/build/sul`; on RISC OS
`SharedULib`, type Module). It goes in `!System.310.Modules` (merge it
with !System). A running SUL can't be replaced while UnixLib programs are
running ("There are still SharedUnixLibrary clients active"): quit them,
or install it and restart.

Program `!Run` files can keep `RMEnsure SharedUnixLibrary 1.16`: 1.17 is
better, not required.

The SWI number is in SUL's own chunk. Before this goes to GCCSDK, the SWI
and the version number have to be agreed there, since GCCSDK releases the
module.

## Where the code is

| File | What |
|---|---|
| `incl-local/internal/ticker.s` | the routines (handler, start, stop, two filters) as one macro |
| `module/sul.s` | SUL: `TICKER_ROUTINES sul_ticker`, `swi_ticker`, `PROC_TICKERBLOCK`, clean-up in `sul_exit` |
| `pthread/_context.s` | UnixLib's copy (`__pthread_call_every_code` … `_end`), offsets, `__pthread_ticker_call` |
| `pthread/ticker.c` | start/stop, the filters, the periodic check, stats |
| `pthread/context.c` | calls `__pthread_ticker_recheck` every 64 switches |
| `pthread/pthinit.c` | `__pthread_ticker_init` at start-up; stats, stop and release at exit |
| `tests/host/ticker`, `tests/emu/ticker_test.py` | host test (C, fake SWIs) and emulator test (the built machine code) |
