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
for p in dsptest miditest nosound ejtest fsrotest tickertest schedtest lfstest heaptest daprobe fxtest eabxtest pmovtest efdtest fwtest dsstest; do
  flags=
  [ $p = lfstest ] && flags=-D_FILE_OFFSET_BITS=64
  arm-riscos-gnueabihf-gcc -O2 -static -fstack-clash-protection $flags -isystem ../../libunixlib/include -L$L $p.c -o out/$p.elf -lm
  GCCSDK_ENV="$GCCSDK_ENV" ../../tools/check-lib.sh out/$p.elf >/dev/null
  "$ELF2AIF" -e out/$p.elf $O/$p,ff8
done
# Each Obey file first prints the release it came from (the git tag, e.g.
# 5.0.3.1-rc8), so a photo of a run shows which build was tested.
# Not TickerChild: TickerStartTask starts it as a Wimp task, where output
# would open a command window.
VER=$(git --no-optional-locks describe --tags 2>/dev/null | sed 's/^v//')
VER=${VER:-unknown}
for f in obey/*,feb; do
  case $f in */TickerChild,feb) cp "$f" $O/; continue;; esac
  awk -v v="$VER" -v n="$(basename "$f" ,feb)" '
    !done && !/^\|/ { print "Echo UnixLibTests " v ": " n; done = 1 }
    { print }
    END { if (!done) print "Echo UnixLibTests " v ": " n }' "$f" > "$O/$(basename "$f")"
done
# The PThreadTicker module built with the library (runs the thread ticker)
cp ../../build/work/build/pthticker $O/PThrTicker,ffa
cp ReadMe $O/ReadMe,fff
# RISC OS filenames aren't case-sensitive: two names differing only in case
# overwrite each other when unzipped (LargeFile vs largefile did).
dups=$(ls $O | sed 's/,[0-9a-f][0-9a-f][0-9a-f]$//' | tr A-Z a-z | sort | uniq -d)
[ -z "$dups" ] || { echo "names clash on RISC OS: $dups" >&2; exit 1; }
python3 ../../tools/mkrozip.py out/UnixLibTests.zip $O
ls -l out/UnixLibTests.zip
