#!/bin/sh -e
# Host tests for sound/dsp.c. Needs a host gcc; SWI pointers must be < 4 GB.
cd "$(dirname "$0")"
mkdir -p out
cp ../../../libunixlib/sound/dsp.c out/dsp.c
cp DRender.h out/
gcc -O1 -g -no-pie -Wall -Wno-unused-function -D_GNU_SOURCE -include fake/prelude.h -Ifake -I. \
  out/dsp.c fake.c test_dsp.c -o out/test_dsp
./out/test_dsp
