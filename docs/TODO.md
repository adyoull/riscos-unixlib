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

(`sched_get_priority_min`/`max` were on this list via riscos-mesa's OpenAL
port; added in 5.0.1. The scheduler still has no real-time policies:
`pthread_setschedparam` refuses them with ENOTSUP.)

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

## 8. Thread ticker: loose ends

The ticker ran while other tasks were paged in (Warzone 2100). The code is
now safe wherever it fires (the PThreadTicker module runs it, or an RMA
copy without the module) and the likely causes are fixed; see
[THREAD-TICKER.md](THREAD-TICKER.md). Still to do:

- Root cause confirmed on the Pi (threads before `Wimp_Initialise`,
  THREAD-TICKER.md). Not yet run: a program without the module
  (`via=RMA`), `*RMKill PThreadTicker` refusing while in use, and the
  Ticker tests in UnixLibTests.zip.
- **Register the name** "PThreadTicker" with RISC OS Open (an allocation,
  not a code submission) before a wide release.
- A program that dies without reaching `_exit` stays attached to the
  module, which then can't be killed until a restart. A clean-up (e.g. the
  module dropping blocks whose program has gone) would need a way to tell;
  not needed so far.
- Paging outside `Wimp_Poll` (e.g. the program's own `Wimp_StartTask`) will
  still make the ticker fire in other tasks. Harmless now; could be
  avoided by stopping the ticker around `Wimp_StartTask`, but UnixLib
  doesn't see that call.
- The interval-timer handlers in `signal/_signal.s` (`__h_sigalrm_init` and
  friends) are also OS_CallEvery handlers in application space.
  `setitimer` refuses to run in a Wimp task (ENOSYS), which avoids the
  problem there; a program that sets one before `Wimp_Initialise` would be
  exposed. The module could run those too if it ever matters.
