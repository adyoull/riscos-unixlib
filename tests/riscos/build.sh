#!/bin/sh -e
# Build the RISC OS tests (sound, exit, fsync) against this repo's libunixlib and zip them.
# GCCSDK_ENV (default ~/gccsdk/env); run build/build-unixlib.sh first.
cd "$(dirname "$0")"
GCCSDK_ENV=${GCCSDK_ENV:-$HOME/gccsdk/env}
PATH=$GCCSDK_ENV/bin:$PATH
L=../../build/work/build/.libs
O=out/UnixLibTests
rm -rf out; mkdir -p $O
for p in dsptest miditest nosound exitjoin fsyncro; do
  arm-riscos-gnueabihf-gcc -O2 -static -fstack-clash-protection -L$L $p.c -o $O/$p,e1f -lm
done
cp obey/*,feb $O/
cp ReadMe $O/ReadMe,fff
python3 ../../tools/mkrozip.py out/UnixLibTests.zip $O
ls -l out/UnixLibTests.zip
