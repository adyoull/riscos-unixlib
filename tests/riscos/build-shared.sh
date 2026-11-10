#!/bin/sh -e
# Build RISC OS tests linked dynamically against this repo's libunixlib.so
# (build/work-shared, from SHARED=yes build/build-unixlib.sh), for checking
# the shared library on RISC OS: they load it through SOManager, as Iris
# does. Output: tests/riscos/out-shared/Tests (ELF programs, type E1F, and
# their Obey files, which print where they came from).
#   GCCSDK_ENV   a toolchain with shared library support (default ~/gccsdk/env)
#   TESTS        the programs (default: the ones that matter for the shared
#                library: shared memory, semaphores, threads, timers, faults)
GCCSDK_ENV=${GCCSDK_ENV:-$HOME/gccsdk/env}
GCCSDK_ENV=$(cd "$GCCSDK_ENV" && pwd)
cd "$(dirname "$0")"
PATH=$GCCSDK_ENV/bin:$PATH
TESTS=${TESTS:-"shmtest semtest preempttest itimertest faulttest"}
L=../../build/work-shared/build/.libs
[ -e $L/libunixlib.so.5 ] || { echo "no $L/libunixlib.so.5: run SHARED=yes build/build-unixlib.sh first" >&2; exit 1; }
O=out-shared/Tests
rm -rf out-shared; mkdir -p $O
VER=$(git --no-optional-locks describe --tags 2>/dev/null | sed 's/^v//')
VER=${VER:-unknown}
for p in $TESTS; do
  # -L finds this repo's libunixlib.so before the toolchain's; the program
  # only records the SONAME (libunixlib.so.5), so at run time it gets
  # whichever libunixlib/so/5 SOManager has.
  arm-riscos-gnueabihf-gcc -O2 -fstack-clash-protection -isystem ../../libunixlib/include \
    -L$L $p.c -o out-shared/$p.elf -lm
  arm-riscos-gnueabihf-readelf -d out-shared/$p.elf | grep -q 'NEEDED.*\[libunixlib\.so\.5\]' ||
    { echo "$p isn't linked dynamically against libunixlib.so.5" >&2; exit 1; }
  # No debug information: it would carry this machine's paths.
  arm-riscos-gnueabihf-objcopy --strip-debug out-shared/$p.elf "$O/$p,e1f"
  # Its Obey file: the one that runs it, with a first line saying what it is.
  f=$(grep -l "^Run <UST\$Dir>\.$p\$" obey/*,feb | head -1)
  [ -n "$f" ] || { echo "no Obey file runs $p" >&2; exit 1; }
  n=$(basename "$f" ,feb)
  awk -v v="$VER" -v n="$n" '
    !done && !/^\|/ { print "Echo UnixLibShared " v ": " n " (shared library)"; done = 1 }
    { print }' "$f" > "$O/$n,feb"
done
ls -l $O
