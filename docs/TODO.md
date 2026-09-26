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

## 2. `/dev/dsp` busy-waits

- **Where:** `sound/dsp.c` write loop:
  `while (DRender_StreamStatistics () >= dr_buffers) pthread_yield ();`.
- **Affects:** SDL's `dsp` audio driver (the fallback when SharedSoundBuffer /
  StreamManager aren't loaded) and any OSS-style program.
- **Workaround today:** the SDL RISC OS audio driver (SharedSoundBuffer) is
  tried first.
- **Possible fix:** yield through the same sleep hook;
  at least return a partial write when the caller opened with `O_NONBLOCK`.

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

## 6. Small things in the merged code

- `wchar/wctype_l.c` (`iswalnum_l` …, from GCCSDK) doesn't have the 0–255
  guard that `wctype.c` now has; a wide character above 255 reads past the
  ctype tables. Same one-line fix.
- `__ul_monotonic_ns` keeps `hr_last_ns` in a static without a lock; two
  threads reading at the same moment could each see the other's value. It
  can't go backwards by more than the race window. Harmless so far.
- The keyboard: SDL's key-up detection polls `OS_Byte 121`; not a UnixLib
  issue, listed in the SDL notes.
