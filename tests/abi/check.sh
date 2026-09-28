#!/bin/sh -e
# ABI checks for the large-file (LFS) interface: struct layouts, which
# symbols calls compile to, the old symbols the library must keep, and a
# C++ program linked with the toolchain's libstdc++ (compiled with the old
# headers).  Needs the cross compiler (GCCSDK_ENV) and a built library.
cd "$(dirname "$0")"
GCCSDK_ENV=${GCCSDK_ENV:-$HOME/gccsdk/env}
P=$GCCSDK_ENV/bin/arm-riscos-gnueabihf
I=../../libunixlib/include
LIB=../../build/work/build/.libs
mkdir -p out
fail=0

# 1. Layouts.  The default mode must never change (existing programs).
for mode in default -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE; do
  flag=$mode; [ "$mode" = default ] && flag=
  $P-gcc -isystem $I $flag -c layout.c -o out/layout.o
  printf '%s:' "$mode"
  $P-nm -S out/layout.o | while read -r a s t n; do printf " %s=%d" "$n" "0x$s"; done | tr ' ' '\n' | sort | tr '\n' ' '
  echo
done > out/layout.txt
if diff -u expected-layout.txt out/layout.txt; then echo "layouts: OK"; else fail=1; fi

# 2. Which symbols the calls go to.
for mode in default -D_FILE_OFFSET_BITS=64; do
  flag=$mode; [ "$mode" = default ] && flag=
  $P-gcc -isystem $I $flag -c calls.c -o out/calls.o
  printf '%s:' "$mode"
  $P-nm -u out/calls.o | awk '{print $2}' | sort | tr '\n' ' '
  echo
done > out/calls.txt
if diff -u expected-calls.txt out/calls.txt; then echo "calls: OK"; else fail=1; fi

# 3. The library still has the old symbols (old layout) and the new ones.
missing=0
for s in stat64 fstat64 lstat64 lseek64 fseeko64 ftello64 fgetpos64 \
	 fsetpos64 fopen64 open64 __unixlib_stat64 __unixlib_fstat64 \
	 __unixlib_lstat64 truncate64 ftruncate64 mmap64; do
  if ! $P-nm "$LIB/libunixlib.a" 2>/dev/null | grep -q " T $s\$"; then
    echo "library: missing $s"; missing=1
  fi
done
if [ $missing = 0 ]; then echo "library symbols: OK"; else fail=1; fi

# 4. A C++ program (libstdc++ calls stat64, fstat64, lseek64...) links.
if $P-g++ -static -isystem $I -L"$LIB" cxx.cc -o out/cxx.elf 2>out/cxx.log; then
  echo "libstdc++ link: OK"
else
  cat out/cxx.log; fail=1
fi

exit $fail
