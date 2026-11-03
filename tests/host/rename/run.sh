#!/bin/sh -e
# Host test for stdio/rename.c with common/riscosify.c: a file's type and
# the type its name implies, over a fake filing system and MimeMap.
# Not PIE: riscosify.c passes the name to MimeMap as an int, so the test's
# names (string literals) must have 32-bit addresses. riscosify.c is built
# with -w (pointer/int casts that are fine on RISC OS); the rest with -Wall.
cd "$(dirname "$0")"
mkdir -p out
L=../../../libunixlib
C="gcc -O1 -g -fno-pie -Ifake -include fake/shim.h -std=gnu99"
$C -w -c $L/common/riscosify.c -o out/riscosify.o
$C -Wall -Werror -c $L/stdio/rename.c -o out/rename.o
$C -Wall -Werror test_rename.c out/riscosify.o out/rename.o -no-pie -o out/test_rename
./out/test_rename
