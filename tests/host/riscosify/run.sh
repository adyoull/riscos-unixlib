#!/bin/sh -e
# Host test for common/riscosify.c: Unix to RISC OS name rules, with every
# SWI failing (no special directories, no MimeMap, no files).
cd "$(dirname "$0")"
mkdir -p out
L=../../../libunixlib
gcc -O1 -g -w -Ifake -include fake/shim.h -std=gnu99 -c $L/common/riscosify.c -o out/riscosify.o
gcc -O1 -g -w -Ifake -include fake/shim.h -std=gnu99 test_riscosify.c out/riscosify.o -o out/test_riscosify
./out/test_riscosify
