# UnixLib: known problems, not fixed yet

Found by the OpenTTD, Warzone 2100 and riscos-mesa (SDL2) work. Each one is
worked around in the program or SDL today. Ordered by how much they hurt.

## 1. Sleeping in a Wimp task freezes the desktop

- **Where:** `signal/sleep.c`, `sleep_int()`. When the program is a Wimp task
  or in a TaskWindow (`__get_taskhandle() != 0`) it does
  `while (clock () < before) pthread_yield ();` — it never calls Wimp_Poll,
  so no other task runs for the whole sleep. The new sub-centisecond tail of
  `nanosleep` busy-waits the same way.
- **Affects:** `sleep`, `usleep`, `nanosleep`, so also C++
  `std::this_thread::sleep_for` and anything built on them.
- **Workarounds today:** the SDL overlay's `SDL_Delay` calls its own
  `RISCOS_WimpDelay` (Wimp_PollIdle) on the main thread. **OpenTTD still
  paces with `sleep_for`**, so its window stops other tasks while it waits
  (noted as open in the riscos-mesa START-HERE).
- **Proposed fix:** a hook. UnixLib gets
  `void __riscos_set_sleep_hook (int (*fn) (clock_t cs, void *ctx), void *ctx)`;
  `sleep_int` calls it (when set, on the main thread, as a Wimp task) instead
  of the busy loop. The SDL RISC OS driver registers a function that does
  Wimp_PollIdle and keeps handling its own Wimp events. Without a hook,
  behaviour stays as now. UnixLib itself must not call Wimp_Poll: it doesn't
  own the program's event loop.

## 2. `/dev/dsp` waits by spinning when its queue is full

- **Where:** `sound/dsp.c`: a blocking write with a full queue loops on
  `pthread_yield()` (both the SharedSoundBuffer and DigitalRenderer paths).
  Other threads run, other Wimp tasks don't (in a Wimp task).
- **Better since the sound work:** `O_NONBLOCK` writes now return a short
  count / `EAGAIN` on the SharedSoundBuffer path, and `SNDCTL_DSP_GETOSPACE`
  is accurate, so a program can write only what fits.
- **Fix:** the sleep hook from #1.

## 3. `>` redirection crashes at start-up in a TaskWindow

- Seen with riscos-mesa's `glbench > file` on the Pi: a crash inside
  `__riscosify` during start-up command line redirection. Not investigated;
  glbench grew a `-o file` option instead.
- **Next step:** reproduce with a tiny program (`int main(){puts("x");}` run
  as `prog > file` in a TaskWindow) and get the crash address.

## 4. `popen()` / `system()` are RISC OS commands

- `popen("which x")` becomes `*which x` → "File 'which' not found" (Warzone
  2100's crash handler set-up). Expected behaviour for UnixLib, but easy to
  trip over. Ports avoid them; the riscos-mesa porting guide warns about it.
- Could be improved by a tiny built-in `which` (search `Run$Path` / the
  path given)? Probably not worth it.

## 5. Missing functions (worked around in Mesa)

`dlopen`, `open_memstream`, `pthread_mutex_timedlock`, pthread barriers,
`pthread_getcpuclockid`, ELF TLS (`__aeabi_read_tp`). `open_memstream` and
`pthread_mutex_timedlock` would be small, self-contained additions.

## 7. Sound: not done yet

- `/dev/dsp` recording (no input).
- `/dev/sequencer` / `/dev/music` (OSS event interface with timing). Only
  raw `/dev/midi` exists.
- `/dev/mixer` (volume): could map to `SharedSoundBuffer_Volume`.
- The MIDISynth module itself (riscos-midisynth project; spec in
  `docs/MIDISYNTH-MODULE.md`).
- `SharedSoundBuffer_Flush` is not used (its arguments aren't documented
  here); `SNDCTL_DSP_RESET` closes the stream and the next write reopens it.
- GCCSDK's GCC 4.7.4 UnixLib (used by PackMan programs such as ffplay) needs
  the same change upstream; see README "Which programs get the changes".

## 6. Small things in the merged code

- `wchar/wctype_l.c` (`iswalnum_l` …, from GCCSDK) doesn't have the 0–255
  guard that `wctype.c` now has; a wide character above 255 reads past the
  ctype tables. Same one-line fix.
- `__ul_monotonic_ns` keeps `hr_last_ns` in a static without a lock; two
  threads reading at the same moment could each see the other's value. It
  can't go backwards by more than the race window. Harmless so far.
- The keyboard: SDL's key-up detection polls `OS_Byte 121`; not a UnixLib
  issue, listed in the SDL notes.

## 8. Thread ticker: why the Wimp filters sometimes don't stop it

The crash this caused (another task running our ticker handler from its
own memory) is fixed: the handler now runs from RMA (0.1.1). What's left is
understanding the gap, since while it exists the ticker still fires 50
times a second while other tasks run (cheap now, but not intended).

- **Threads started before `Wimp_Initialise`** (for example an SDL audio or
  timer thread created before the window): `__pthread_start_ticker` finds
  task handle 0, starts the ticker and registers **no** filters. They are
  only registered at the next `pthread_create`, which may never come.
  `TickerEarly` in the Pi tests does exactly this. Possible fix: register
  the filters later, e.g. check once per context switch (in USR mode, not
  in the callback) whether the program has become a Wimp task.
- **The cached task handle** (`_syslib.s` reads `Wimp_ReadSysInfo 5` at
  start-up, before `Wimp_Initialise`, and `__pthread_start_ticker` only
  re-reads it when it's 0). If that value isn't the program's final task
  handle, the filters are registered for another task. `Ticker` logs both
  values; check on the Pi.
- **Task switches that don't go through our `Wimp_Poll`,** e.g. our own
  `Wimp_StartTask` (Warzone starts URIdispatch for web links): the child
  runs with us paged out and no pre-filter call.
- If a program dies without reaching `_exit` (so `__pthread_prog_fini`
  never removes the ticker), the handler and its RMA block now stay behind:
  harmless (it only checks the upcall handler and returns) but a leak of
  248 bytes and a 2 cs ticker until reset. Before, it jumped into whatever
  was loaded at that address.
- The fix assumes RMA is executable (true on RISC OS 5 today). If that ever
  changes, the handler would have to live in SharedUnixLibrary.
- The interval-timer handlers in `signal/_signal.s` (`__h_sigalrm_init` and
  friends) are also OS_CallEvery handlers in application space.
  `setitimer` refuses to run in a Wimp task (ENOSYS), which avoids the
  problem there; a program that sets one before `Wimp_Initialise` would be
  exposed. Same RMA treatment if it ever matters.

