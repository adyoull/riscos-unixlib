#!/bin/sh -e
# Build everything for a GitHub release from a clean tree at a tag:
#   tools/make-release.sh vX.Y.Z[-rcN]
# Output: out/release/<tag>/ (libunixlib.a, patches, UnixLibTests.zip,
# PThreadTicker-<version>.zip for RISC OS users, SHA256SUMS).
# Needs everything `make lib` and `make riscos-tests` need.
cd "$(dirname "$0")/.."
TAG=$1
[ -n "$TAG" ] || { echo "usage: $0 vX.Y.Z[-rcN]" >&2; exit 1; }
git diff --quiet HEAD || { echo "uncommitted changes; commit first" >&2; exit 1; }
git rev-parse -q --verify "refs/tags/$TAG" >/dev/null ||
  { echo "no tag $TAG; tag the release commit first" >&2; exit 1; }
[ "$(git rev-parse HEAD)" = "$(git rev-parse "$TAG^{commit}")" ] ||
  { echo "HEAD is not $TAG" >&2; exit 1; }

rm -rf build/work
build/build-unixlib.sh
tests/check.sh
tests/riscos/build.sh

OUT=out/release/$TAG
rm -rf "$OUT"; mkdir -p "$OUT"
B=build/work/build

# PThreadTicker for RISC OS users: a !System to merge, and a ReadMe
MODVER=$(sed -n 's/.*"PThreadTicker\\t\([0-9.]*\) .*/\1/p' libunixlib/module/pthticker.s)
[ -n "$MODVER" ] || { echo "can't read the module version" >&2; exit 1; }
M=$OUT/tmp/PThreadTicker
mkdir -p "$M/!System/310/Modules"
cp "$B/pthticker" "$M/!System/310/Modules/PThrTicker,ffa"
case $TAG in
  *-rc*) STATUS="** Pre-release: not yet tested on RISC OS hardware. **" ;;
  *) STATUS= ;;
esac
awk -v v="$MODVER" -v t="$TAG" -v st="$STATUS" \
  '{ gsub(/VERSION/, v); gsub(/TAG/, t) } /^STATUS$/ { if (st != "") print st "\n"; next } { print }' \
  release/PThreadTicker-ReadMe > "$M/ReadMe,fff"
cp release/PThreadTicker-Licence "$M/Licence,fff"
python3 tools/mkrozip.py "$OUT/PThreadTicker-$MODVER.zip" "$M"
rm -rf "$OUT/tmp"

cp "$B/.libs/libunixlib.a" patches/unixlib-riscos.diff patches/unixlib-sound.diff \
   tests/riscos/out/UnixLibTests.zip "$OUT/"
# No paths from the build machine in anything we ship (the zips are
# checked by their contents).
leak=$(for f in "$OUT"/*; do
         case $f in *.zip) unzip -p "$f" ;; *) cat "$f" ;; esac
       done | strings -a | grep -F -e "$PWD" -e "$HOME" | sort -u)
[ -z "$leak" ] || { echo "release files contain build paths:" >&2; echo "$leak" >&2; exit 1; }
(cd "$OUT" && sha256sum * > SHA256SUMS)
ls -l "$OUT"
