#!/bin/sh -e
# Host test for unix/eventfd.c (the counter's read-modify-write).
cd "$(dirname "$0")"
mkdir -p out
gcc -O1 -g -Wall -Ifake ../../../libunixlib/unix/eventfd.c ../../../libunixlib/pthread/heldwait.c test_eventfd.c -o out/test_eventfd
./out/test_eventfd
