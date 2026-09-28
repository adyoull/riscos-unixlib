# Files over 2GB

RISC OS file pointers and extents are unsigned 32-bit values, so a file
can be up to **4GB-1** bytes (FileCore after ROOL's "File system
improvements" bounty, DOSFS/FAT32). There is no 64-bit FileSwitch API, so
4GB-1 is the ceiling for now.

UnixLib's `off_t` is a signed 32-bit `long` (2GB-1), as on other 32-bit
systems. Programs reach further the usual way, with the Large File Support
(LFS) interface: compile with **`-D_FILE_OFFSET_BITS=64`** and `off_t`,
`fpos_t` and `struct stat`'s `st_size` become 64-bit, and `lseek`, `stat`,
`fseeko`... go to 64-bit versions. That is also what ROOL's SharedCLibrary
does ("Migrating C software to 64-bit file pointers" on the ROOL wiki).

Up to 5.0.1 the 64-bit versions existed but stopped at 2GB-1, and
`struct stat64` had a 32-bit `st_size`. From the next release (5.0.2)
they reach 4GB-1.

## For programs

- Build with `-D_FILE_OFFSET_BITS=64` (many configure scripts, FFmpeg's
  among them, already do) and relink. Use `fseeko`/`ftello`, not
  `fseek`/`ftell` (those take a `long` by the C standard).
- Or use the explicit names with `-D_LARGEFILE64_SOURCE`: `off64_t`,
  `struct stat64`, `lseek64`, `fseeko64`, `ftello64`, `fgetpos64`,
  `fsetpos64`, `stat64`, `fstat64`, `lstat64`, `truncate64`,
  `ftruncate64`, `mmap64`.
- Positions past 4GB-1 fail with `EOVERFLOW` (seeking) or `EFBIG`
  (truncating), negative ones with `EINVAL`.
- Without `_FILE_OFFSET_BITS=64` nothing changes. `read` and `write` work
  on any file as before; `lseek`, `stat` and friends still report
  positions and sizes of 2GB and over as negative numbers, as they always
  have.
- Whether a particular filing system allows files over 2GB is up to it
  (older FileCore versions, network filing systems...).

## Compatibility (what doesn't change)

UnixLib is linked statically, so programs already built don't change at
all. What matters is mixing **objects and libraries built with older
headers** into a new program:

| | 5.0.1 | 5.0.2 |
|---|---|---|
| `struct stat` (default) | 64 bytes, `st_size` 32-bit at 28 | **unchanged** |
| `struct stat` with `_FILE_OFFSET_BITS=64`, `struct stat64` | same as `struct stat` | 72 bytes, `st_size` 64-bit at 32 |
| `stat`/`fstat`/`lstat` with `_FILE_OFFSET_BITS=64` call | `stat64`... | `__unixlib_stat64`... |
| symbols `stat64`, `fstat64`, `lstat64` | 32-bit `st_size` | **unchanged**: still the old layout |
| `lseek64`, `fseeko64`, `ftello64`, `fgetpos64` | up to 2GB-1 | up to 4GB-1 (same signatures) |
| `fsetpos64` | broken (used the pointer, not the position) | fixed |
| `truncate`/`ftruncate` with `_FILE_OFFSET_BITS=64` | 32-bit length | `truncate64`/`ftruncate64` |
| `mmap` with `_FILE_OFFSET_BITS=64` | offset passed in the wrong place | `mmap64` |
| `FILE`, `fpos_t`, `off_t` | | unchanged in every mode |

The new `struct stat64` layout comes with **new symbol names**, so old
objects that call `stat64`/`fstat64`/`lstat64` with the small structure
(the toolchain's `libstdc++.a` and `libstdc++fs.a` do, and so will any
static library built with `_FILE_OFFSET_BITS=64` against 5.0.1 headers)
keep working. New code gets the new names through the headers.

One thing to avoid: passing a `struct stat` between an object compiled
with `_FILE_OFFSET_BITS=64` against the new headers and one compiled with
it against the old ones. Rebuild both.

`tests/abi/check.sh` (part of `make check`) checks all of this: layouts in
each mode against `tests/abi/expected-layout.txt`, which symbols the calls
compile to, that the old symbols are still exported, and that a C++ program
links with the toolchain's libstdc++.

## Where it is

| File | What |
|---|---|
| `include/sys/stat.h` | `struct stat64`, the redirects to `__unixlib_*stat64` |
| `unix/stat64.c`, `unix/fstat.c`, `unix/scl_fstat.c` | `__unixlib_stat64`, `__unixlib_lstat64`, `__unixlib_fstat64` |
| `unix/stat.c`, `unix/lstat.c`, `unix/fstat.c` | the old `stat64`/`lstat64`/`fstat64` symbols (asm names) |
| `incl-local/sys/stat.h` | `__stat_to_stat64` (size read as unsigned) |
| `unix/ul_lseek.c`, `unix/dev.c` | `lseek64`, `__fslseek64` |
| `stdio/fseeko.c`, `ftello.c`, `fgetpos.c`, `fsetpos.c` | the stdio 64-bit calls |
| `unix/truncate.c`, `include/unistd.h` | `truncate64`, `ftruncate64` |
| `sys/mmap64.c`, `include/sys/mman.h` | `mmap64` |
| `tests/abi/`, `tests/riscos/largefile.c` | ABI check, Pi test (`LargeFile`) |
