#!/bin/sh -e
# Host test for pthread/heldwait.c (waiting inside a held call).
cd "$(dirname "$0")"
mkdir -p out
gcc -O1 -g -Wall -Ifake ../../../libunixlib/pthread/heldwait.c test_heldwait.c -o out/test_heldwait
./out/test_heldwait
