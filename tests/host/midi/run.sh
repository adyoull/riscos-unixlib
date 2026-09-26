#!/bin/sh -e
# Host tests for sound/midi.c (fake SWIs; pointers must be < 4 GB).
cd "$(dirname "$0")"
mkdir -p out
F=../dsp/fake
gcc -O1 -g -no-pie -Wall -D_GNU_SOURCE -include $F/prelude.h -I$F -I/usr/include \
  -Dfake_yield=fake_yield_unused -Dfake_clock=fake_clock_unused \
  ../../../libunixlib/sound/midi.c test_midi.c -o out/test_midi
./out/test_midi
