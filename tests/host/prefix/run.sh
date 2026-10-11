#!/bin/sh -e
# Host test for common/prefix.c (__get_dde_prefix).
cd "$(dirname "$0")"
mkdir -p out
gcc -O1 -g -Wall -Ifake ../../../libunixlib/common/prefix.c test_prefix.c -o out/test_prefix
./out/test_prefix
