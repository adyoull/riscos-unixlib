#!/bin/sh -e
# Host tests for sound/dsp.c against the fake RISC OS in ../fake.
# dsp.c is copied into out/ first so that its #include "DRender.h" picks up
# the fake DigitalRenderer (../fake/DRender.h) instead of the real one.
cd "$(dirname "$0")"
F=../fake
mkdir -p out
cp ../../../libunixlib/sound/dsp.c out/dsp.c
cp $F/DRender.h out/
gcc -O1 -g -no-pie -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-unused-value -D_GNU_SOURCE -include $F/prelude.h -I$F \
  out/dsp.c $F/riscos.c test_dsp.c -o out/test_dsp
./out/test_dsp
