#!/bin/sh -e
# Check that a libunixlib.a, or a program linked with it, was built
# consistently: the start-up code (sys/_syslib.s) must claim the whole
# pthread RMA block, including the room for the ticker handler that
# pthread/pthinit.c copies into it.
#
#   tools/check-lib.sh [libunixlib.a | program ELF] ...
#   (default: build/work/build/.libs/libunixlib.a)
#
# Why: an incremental build once kept an old _syslib.o that claimed the old
# 120-byte block; the 72-byte handler was then copied past its end, which
# corrupted the RMA and hung the machine (Warzone 2100 riscos14/15).
# The Makefile now rebuilds assembler files when asm_dec.s changes, and
# __pthread_prog_init stops with an error on a mismatch; this catches it
# before the program ever runs.  Same idea as riscos-warzone2100's
# tools/check-unixlib.sh.
REPO=$(cd "$(dirname "$0")/.." && pwd)
GCCSDK_ENV=${GCCSDK_ENV:-$HOME/gccsdk/env}
OBJDUMP=$GCCSDK_ENV/bin/arm-riscos-gnueabihf-objdump
NM=$GCCSDK_ENV/bin/arm-riscos-gnueabihf-nm
# sizeof (struct __pthread_callevery_block); pthinit.c asserts the same.
EXPECTED=${EXPECTED:-248}
[ $# -gt 0 ] || set -- "$REPO/build/work/build/.libs/libunixlib.a"

bad=0
for f in "$@"; do
  if ! "$NM" "$f" 2>/dev/null | grep -q ' __pthread_call_every_code$'; then
    echo "$f: no __pthread_call_every_code (UnixLib without the RMA ticker fix)" >&2
    bad=1; continue
  fi
  # "mov r3, #<size>" then OS_Module (svc 0x2001e) in no_dynamic_area.
  size=$("$OBJDUMP" -d "$f" | awk '/<no_dynamic_area>:$/{p=1;next} p&&/^$/{exit}
    p&&/mov\tr3, #/{s=$0} p&&/svc\t0x0002001e/{sub(/.*#/,"",s); sub(/[ \t;].*/,"",s); print s; exit}')
  case $size in
    "$EXPECTED") echo "$f: OK (claims the $EXPECTED-byte pthread RMA block)" ;;
    "") echo "$f: can't find the pthread RMA block claim" >&2; bad=1 ;;
    *) echo "$f: WRONG: claims $size bytes, not $EXPECTED. Rebuild UnixLib from clean (make clean; make lib)." >&2
       bad=1 ;;
  esac
done
exit $bad
