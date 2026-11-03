#!/bin/sh -e
# Host test for pthread/sem.c (semaphores that block, sem_timedwait), with
# a simulated clock and a model of the context switcher's timed waits.
# Uses the real include/semaphore.h.
cd "$(dirname "$0")"
mkdir -p out/inc
L=../../../libunixlib
cp $L/include/semaphore.h out/inc/semaphore.h
C="gcc -O1 -g -Wall -Werror -std=gnu11 -Ifake -Iout/inc -include fake/prelude.h"
$C -c $L/pthread/sem.c -o out/sem.o
# No allocation any more: a sem_wait can't leak.
if nm out/sem.o | grep -q ' U malloc\| U free'; then
  echo "FAIL: sem.o still calls malloc/free"; exit 1
fi
$C test_sem.c out/sem.o -o out/test_sem
./out/test_sem
