#!/bin/sh -e
# Host test for stdlib/mkdtemp.c (H3, from riscos-unixlib-web W5).
cd "$(dirname "$0")"
mkdir -p out
gcc -O1 -g -Wall -Wno-deprecated-declarations ../../../libunixlib/stdlib/mkdtemp.c test_mkdtemp.c -o out/test_mkdtemp 2>&1 | grep -v -e "is dangerous" -e "in function .mkdtemp." || true
./out/test_mkdtemp
