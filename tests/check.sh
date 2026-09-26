#!/bin/sh -e
# Everything that can be checked without RISC OS or the cross compiler.
# Needs: a host C compiler (gcc), git, patch, python3.  Run from anywhere.
cd "$(dirname "$0")/.."

echo "== host tests: /dev/dsp"
tests/host/dsp/run.sh
echo "== host tests: /dev/midi"
tests/host/midi/run.sh

echo "== patches match the history"
tools/make-patches.sh --check

echo "== patches apply to unchanged GCCSDK UnixLib"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
IMPORT=$(git rev-list --max-parents=0 HEAD)
mkdir -p "$tmp/gcc4/recipe/files/gcc"
git archive "$IMPORT" libunixlib | tar x -C "$tmp/gcc4/recipe/files/gcc"
for p in patches/unixlib-riscos.diff patches/unixlib-sound.diff; do
  patch -d "$tmp" -p1 --dry-run -s < "$p"
  echo "$p applies"
done

echo "== scripts parse"
for s in build/*.sh tests/*.sh tests/host/*/run.sh tests/riscos/build.sh tools/*.sh; do
  sh -n "$s"
done
python3 -m py_compile tools/mkrozip.py
rm -rf tools/__pycache__
echo "== all checks passed"
