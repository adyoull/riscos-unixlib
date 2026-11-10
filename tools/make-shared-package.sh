#!/bin/sh -e
# Build the shared library package for a release, from a clean tree at
# its tag:
#   tools/make-shared-package.sh vX.Y.Z[.W][-rcN]
# Output: out/release/<tag>-shared/
#   UnixLibShared-<version>.zip  libunixlib/so/5 (and a Trace variant) for
#                                a program's !SharedLibs, dynamically
#                                linked tests, ReadMe (RISC OS filetypes)
#   libunixlib.so.<v>-debug, libunixlib.so.<v>-trace-debug  unstripped, to
#                                turn addresses into names
#   SHA256SUMS
# Needs GCCSDK_ENV to be a toolchain with shared library support (see
# docs/MAINTAINING.md), plus what `make lib` needs. JOBS as for
# build/build-unixlib.sh.
cd "$(dirname "$0")/.."
TAG=$1
[ -n "$TAG" ] || { echo "usage: $0 vX.Y.Z[.W][-rcN]" >&2; exit 1; }
git diff --quiet HEAD || { echo "uncommitted changes; commit first" >&2; exit 1; }
git rev-parse -q --verify "refs/tags/$TAG" >/dev/null ||
  { echo "no tag $TAG; tag the release commit first" >&2; exit 1; }
[ "$(git rev-parse HEAD)" = "$(git rev-parse "$TAG^{commit}")" ] ||
  { echo "HEAD is not $TAG" >&2; exit 1; }
GCCSDK_ENV=${GCCSDK_ENV:-$HOME/gccsdk/env}
export GCCSDK_ENV
VER=${TAG#v}
OBJCOPY=$GCCSDK_ENV/bin/arm-riscos-gnueabihf-objcopy

# The library (with its ABI check), the trace variant, the tests.
SHARED=yes build/build-unixlib.sh
SHARED=yes TRACE=yes build/build-unixlib.sh
tests/riscos/build-shared.sh
SO=$(readlink -f build/work-shared/build/.libs/libunixlib.so.5)
SOT=$(readlink -f build/work-shared-trace/build/.libs/libunixlib.so.5)
SOV=$(basename "$SO")			# libunixlib.so.5.<age>.<rev>

OUT=out/release/$TAG-shared
rm -rf "$OUT"; mkdir -p "$OUT"
P=$OUT/tmp/UnixLibShared-$VER
mkdir -p "$P/lib/armeabihf" "$P/Trace/lib/armeabihf"
"$OBJCOPY" --strip-debug "$SO" "$P/lib/armeabihf/libunixlib.so.5,e1f"
"$OBJCOPY" --strip-debug "$SOT" "$P/Trace/lib/armeabihf/libunixlib.so.5,e1f"
cp -r tests/riscos/out-shared/Tests "$P/Tests"
case $TAG in
  *-rc*) STATUS="** Pre-release: not yet tested on RISC OS hardware. **" ;;
  *) STATUS= ;;
esac
awk -v v="$VER" -v t="$TAG" -v st="$STATUS" \
  '{ gsub(/VERSION/, v); gsub(/TAG/, t) } /^STATUS$/ { if (st != "") print st "\n"; next } { print }' \
  release/UnixLibShared-ReadMe > "$P/ReadMe,fff"
python3 tools/mkrozip.py "$OUT/UnixLibShared-$VER.zip" "$P"
rm -rf "$OUT/tmp"
cp "$SO" "$OUT/$SOV-debug"
cp "$SOT" "$OUT/$SOV-trace-debug"

# No paths from the build machine in what goes to RISC OS users (the
# debug copies record the source files under /riscos-unixlib only).
leak=$(for f in "$OUT"/*; do
         case $f in *.zip) unzip -p "$f" ;; *) cat "$f" ;; esac
       done | strings -a | grep -F -e "$PWD" -e "$HOME" | sort -u)
[ -z "$leak" ] || { echo "package contains build paths:" >&2; echo "$leak" >&2; exit 1; }
(cd "$OUT" && sha256sum * > SHA256SUMS)
ls -l "$OUT"
