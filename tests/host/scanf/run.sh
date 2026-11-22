#!/bin/sh -e
# Host test for stdio/scanf.c (length modifiers): vfscanf and __sccl are
# taken from the real file, renamed (ul_vfscanf), with UnixLib's FILE
# peeking replaced by getc/ungetc, and run on the PC through fmemopen.  On
# the PC a long is 64 bits, so this checks the conversions and the sizes
# stored, not the 32-bit long of RISC OS.
cd "$(dirname "$0")"
mkdir -p out
S=../../../libunixlib/stdio/scanf.c
{ printf '#define vfscanf ul_vfscanf\n'
  printf 'static int host_peekc (FILE *f) { int c = getc (f); if (c != EOF) ungetc (c, f); return c; }\n'
  # everything up to the string-stream functions, which use UnixLib's FILE
  sed -n '1,/^int vsscanf/p' $S | sed '$d' \
    | sed 's/^#define peekc(stream).*/#define peekc(stream) host_peekc (stream)/'
} > out/scanf_host.c
gcc -O1 -g -w -Ifake -include stdio.h out/scanf_host.c -c -o out/scanf.o
gcc -O1 -g -Wall test_scanf.c out/scanf.o -o out/test_scanf
./out/test_scanf
