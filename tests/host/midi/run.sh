#!/bin/sh -e
# Host tests for sound/midi.c.  Uses the fake headers in ../fake; the test
# file has its own fake SWIs (MIDISynth module and MIDI module).
cd "$(dirname "$0")"
F=../fake
mkdir -p out
gcc -O1 -g -no-pie -Wall -Wno-int-to-pointer-cast -D_GNU_SOURCE -include $F/prelude.h -I$F \
  ../../../libunixlib/sound/midi.c test_midi.c -o out/test_midi
./out/test_midi
