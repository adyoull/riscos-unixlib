#!/bin/sh -e
# Everything that can be checked without RISC OS or the cross compiler.
# Needs: a host C compiler (gcc), git, patch, python3.  Run from anywhere.
cd "$(dirname "$0")/.."

echo "== host tests: /dev/dsp"
tests/host/dsp/run.sh
echo "== host tests: /dev/midi"
tests/host/midi/run.sh
echo "== host tests: thread ticker"
tests/host/ticker/run.sh
echo "== host tests: wide characters (swprintf, wcsftime, wcsto*)"
tests/host/wchar/run.sh

echo "== emulator: thread ticker machine code (needs a build + unicorn)"
GCCSDK_ENV=${GCCSDK_ENV:-$HOME/gccsdk/env}
if [ -f build/work/build/sul ] && [ -x "$GCCSDK_ENV/bin/arm-riscos-gnueabihf-nm" ] &&
   python3 -c 'import unicorn' 2>/dev/null; then
  python3 tests/emu/ticker_test.py
else
  echo "SKIP (no build/work/build, cross toolchain or python3 unicorn module)"
fi

echo "== emulator: SWI wrappers in library code (needs a build + unicorn)"
if [ -f build/work/build/.libs/libunixlib.a ] && [ -x "$GCCSDK_ENV/bin/arm-riscos-gnueabihf-gcc" ] &&
   python3 -c 'import unicorn' 2>/dev/null; then
  python3 tests/emu/swi_test.py
else
  echo "SKIP (no build/work/build, cross toolchain or python3 unicorn module)"
fi

echo "== emulator: malloc across several heap dynamic areas (needs a build + unicorn)"
if [ -f build/work/build/.libs/libunixlib.a ] && [ -x "$GCCSDK_ENV/bin/arm-riscos-gnueabihf-gcc" ] &&
   python3 -c 'import unicorn' 2>/dev/null; then
  python3 tests/emu/heap_test.py
else
  echo "SKIP (no build/work/build, cross toolchain or python3 unicorn module)"
fi

echo "== ABI: large-file interface (needs a build + the cross compiler)"
if [ -f build/work/build/.libs/libunixlib.a ] && [ -x "$GCCSDK_ENV/bin/arm-riscos-gnueabihf-gcc" ]; then
  tests/abi/check.sh
else
  echo "SKIP (no build/work/build or cross toolchain)"
fi

echo "== patches match the history"
tools/make-patches.sh --check

echo "== patches apply to unchanged GCCSDK UnixLib"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
IMPORT=$(tools/import-commit.sh)
mkdir -p "$tmp/gcc4/recipe/files/gcc"
git archive "$IMPORT" libunixlib | tar x -C "$tmp/gcc4/recipe/files/gcc"
for p in patches/unixlib-riscos.diff patches/unixlib-sound.diff; do
  patch -d "$tmp" -p1 --dry-run -s < "$p"
  echo "$p applies"
done

echo "== scripts parse"
for s in build/*.sh tests/*.sh tests/abi/check.sh tests/host/*/run.sh tests/riscos/build.sh tools/*.sh; do
  sh -n "$s"
done
python3 -m py_compile tools/mkrozip.py tests/emu/ticker_test.py tests/emu/swi_test.py tests/emu/heap_test.py
rm -rf tools/__pycache__ tests/emu/__pycache__
echo "== all checks passed"
