#!/bin/sh -e
# Host test for netlib/ul_socketpair.c (N3, from riscos-unixlib-web W8).
cd "$(dirname "$0")"
mkdir -p out
gcc -O1 -g -Wall -Dsocketpair=ul_socketpair '-D__set_errno(e)=(errno = (e), -1)' \
  ../../../libunixlib/netlib/ul_socketpair.c test_socketpair.c -o out/test_socketpair
./out/test_socketpair
