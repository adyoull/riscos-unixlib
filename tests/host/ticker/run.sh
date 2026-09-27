#!/bin/sh -e
# Host tests for pthread/ticker.c (starting/stopping the thread ticker, the
# Wimp filters, SharedUnixLibrary or the RMA copy, the statistics line).
# ./fake shadows UnixLib's internal headers with the few fields used.
cd "$(dirname "$0")"
mkdir -p out
gcc -O1 -g -no-pie -Wall -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast \
  -D_GNU_SOURCE -Dgetenv=fake_getenv -Ifake -I../fake \
  ../../../libunixlib/pthread/ticker.c test_ticker.c -o out/test_ticker
./out/test_ticker
