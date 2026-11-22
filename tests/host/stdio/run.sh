#!/bin/sh -e
# Host test for stdio/fread.c and stdio/fwrite.c (short transfers), with a
# shim of UnixLib's FILE (fake/stdio.h).
cd "$(dirname "$0")"
mkdir -p out
gcc -O1 -g -w -Ifake ../../../libunixlib/stdio/fread.c ../../../libunixlib/stdio/fwrite.c \
  test_stdio.c -o out/test_stdio
./out/test_stdio
