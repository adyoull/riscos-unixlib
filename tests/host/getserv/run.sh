#!/bin/sh -e
# Host test for netlib/getserv_r.c (getservbyname_r and friends).
cd "$(dirname "$0")"
mkdir -p out
gcc -O1 -g -Wall -Ifake ../../../libunixlib/netlib/getserv_r.c test_getserv.c -o out/test_getserv
./out/test_getserv
