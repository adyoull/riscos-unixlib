#!/bin/sh -e
# Fetch the sources the build needs into build/src (or $SRC_DIR) and check
# them.  Safe to run again: anything already there and correct is kept.
#
#   build/src/riscos-gccsdk   GCCSDK at 64c6f81 (checked by commit hash)
#   build/src/gcc-10.2.0      GCC 10.2.0 source (checked by sha256)
#
# The cross compiler itself is not fetched; see docs/MAINTAINING.md
# ("Toolchain").  Afterwards:
#   GCC_SRC=build/src/gcc-10.2.0 GCCSDK_SRC=build/src/riscos-gccsdk \
#   GCCSDK_ENV=~/gccsdk/env build/build-unixlib.sh
#
# If a download is blocked, put the file in build/src by hand (for GCC:
# gcc-10.2.0.tar.gz) and run this again; it only checks what's there.
cd "$(dirname "$0")/.."
. build/sources.conf
SRC_DIR=${SRC_DIR:-build/src}
mkdir -p "$SRC_DIR"

# GCCSDK: only the parts UnixLib's build and elf2aif use.
G="$SRC_DIR/riscos-gccsdk"
if [ ! -d "$G/.git" ]; then
  git clone -q --filter=blob:none --no-checkout "$GCCSDK_URL" "$G"
  git -C "$G" sparse-checkout set autobuilder/develop/gcc gcc4/riscos/elf2aif
fi
git -C "$G" -c advice.detachedHead=false checkout -q "$GCCSDK_COMMIT"
got=$(git -C "$G" rev-parse HEAD)
case "$got" in
  "$GCCSDK_COMMIT"*) echo "GCCSDK: $got OK" ;;
  *) echo "GCCSDK: at $got, wanted $GCCSDK_COMMIT" >&2; exit 1 ;;
esac

# GCC source (the build only uses its top-level files and config/).
T="$SRC_DIR/gcc-10.2.0.tar.gz"
if [ ! -f "$T" ]; then
  curl -fL -o "$T.part" "$GCC_URL" && mv "$T.part" "$T"
fi
sum=$(sha256sum "$T" | cut -d' ' -f1)
if [ "$sum" != "$GCC_SHA256" ]; then
  echo "GCC tarball sha256 $sum" >&2
  echo "        expected    $GCC_SHA256" >&2
  echo "Any GCC 10.2.0 source works for this build; if you trust this one," >&2
  echo "run again with GCC_SHA256=$sum" >&2
  exit 1
fi
echo "GCC 10.2.0: sha256 OK"
if [ ! -d "$SRC_DIR/gcc-10.2.0" ]; then
  mkdir -p "$SRC_DIR/gcc-unpack"
  tar xzf "$T" -C "$SRC_DIR/gcc-unpack"
  mv "$SRC_DIR"/gcc-unpack/* "$SRC_DIR/gcc-10.2.0"
  rmdir "$SRC_DIR/gcc-unpack"
fi
echo "Sources ready in $SRC_DIR"
