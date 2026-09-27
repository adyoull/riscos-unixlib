#!/bin/sh -e
# Build the RISC OS tests (sound, exit, fsync) against this repo's libunixlib and zip them.
# GCCSDK_ENV (default ~/gccsdk/env); run build/build-unixlib.sh first.
cd "$(dirname "$0")"
GCCSDK_ENV=${GCCSDK_ENV:-$HOME/gccsdk/env}
# elf2aif with the large-image fix (tools/elf2aif; built here if needed). The tests
# ship as Absolute (AIF, ,ff8) files: they run without the ELF loader.
ELF2AIF=${ELF2AIF:-../../tools/elf2aif/elf2aif}
[ -x "$ELF2AIF" ] || make -C ../../tools/elf2aif >/dev/null
PATH=$GCCSDK_ENV/bin:$PATH
L=../../build/work/build/.libs
O=out/UnixLibTests
rm -rf out; mkdir -p $O
for p in dsptest miditest nosound exitjoin fsyncro tickertest schedtest; do
  arm-riscos-gnueabihf-gcc -O2 -static -fstack-clash-protection -isystem ../../libunixlib/include -L$L $p.c -o out/$p.elf -lm
  GCCSDK_ENV="$GCCSDK_ENV" ../../tools/check-lib.sh out/$p.elf >/dev/null
  "$ELF2AIF" -e out/$p.elf $O/$p,ff8
done
cp obey/*,feb $O/
# The PThreadTicker module built with the library (runs the thread ticker)
cp ../../build/work/build/pthticker $O/PThrTicker,ffa
cp ReadMe $O/ReadMe,fff
python3 ../../tools/mkrozip.py out/UnixLibTests.zip $O
ls -l out/UnixLibTests.zip
