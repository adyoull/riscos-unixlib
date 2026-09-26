# Changelog

## Unreleased (2026-09-26) — first release of this repo

Merges every UnixLib change made by the RISC OS ports so far. Base: GCCSDK
`64c6f81` (jhamby/riscos-gccsdk), imported unchanged.

### From the OpenTTD port (riscos-openttd `patches/unixlib`, used since 14.1-riscos1)

- **Wide characters** (`wchar/wmissing.c`, `wchar/wctype.c`): real versions of
  the functions that were "Not implemented" + `abort()`: `wctype`,
  `wctype_l`, `iswctype`, `iswctype_l`, `iswupper`, `iswlower`, `iswblank`
  (and `_l`), `wcscoll`, `wcsxfrm` (and `_l`), `wcscasecmp`, `wcsncasecmp`
  (and `_l`), `wcstol`, `wcstoul`, `wcstoll`, `wcstoull`, `wcstod`,
  `wcstof`, `wcstold`, `swprintf` (no `%ls`), `wcsftime`, `putwc`, `getwc`,
  `ungetwc`. 8-bit/Latin-1, matching UnixLib's ctype tables. The `isw*`
  functions in `wctype.c` return 0 above 255 instead of reading past the
  tables. Fixes C++ programs dying at start with "wctype: Not implemented"
  (libstdc++ `std::locale`). Confirmed on the Pi by OpenTTD and Warzone 2100.
- **High resolution monotonic clock** (`time/clk_gettime.c`):
  `CLOCK_MONOTONIC` = monotonic centiseconds + HAL counter position within
  the centisecond. Checks the counter reloads once per centisecond, else
  falls back. Never goes backwards. Removed a regular stutter in OpenTTD.
- **nanosleep** (`signal/sleep.c`): whole centiseconds via the old sleep, the
  rest waited out on the new clock with `pthread_yield()`. `EINVAL` is now
  returned (it used to set errno and carry on). `rem` is set when
  interrupted.
- **malloc** (`stdlib/alloc.c`): `DEFAULT_MMAP_MAX 0` on EABI. Large blocks
  come from the heap dynamic area instead of leaking `mmap#N` areas.

### From the SDL2 / riscos-mesa work

No UnixLib source changes: the SDL overlay works around UnixLib (its own
Wimp_PollIdle delay, its own sound driver). The UnixLib problems it found are
recorded in [docs/TODO.md](docs/TODO.md) for a later release.

### Repo

- `patches/unixlib-riscos.diff`: the whole change set for a GCCSDK checkout.
- `build/build-unixlib.sh`: rebuild just `libunixlib.a` for an installed
  toolchain (reproduces the Warzone toolchain's library exactly).
