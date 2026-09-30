#!/bin/bash -e
# Build libunixlib.a (and optionally the shared library) from this repo's
# libunixlib/ with an installed GCCSDK GCC 10 cross compiler, without
# rebuilding GCC.
#
#   GCC_SRC=<gcc-10.2.0 source>        top-level files (config/, libtool.m4,
#                                      ltmain.sh, config-ml.in ...) are needed;
#                                      default build/src/gcc-10.2.0
#   GCCSDK_SRC=<riscos-gccsdk checkout> for autobuilder/develop/gcc/libtool.m4.p;
#                                      default build/src/riscos-gccsdk
#   (build/fetch-sources.sh fetches and checks both)
#   GCCSDK_ENV=<installed env>         default ~/gccsdk/env
#   CFLAGS                             default "-g -O2 -fstack-clash-protection"
#                                      (-fdebug-prefix-map is always added)
#   INSTALL=yes                        also copy libunixlib.a and UnixLib's
#                                      headers into GCCSDK_ENV
#
# Needs autoconf2.69, automake 1.11 (aclocal-1.11/automake-1.11), perl.
# Output: build/work/build/.libs/libunixlib.a
REPO=$(cd "$(dirname "$0")/.." && pwd)
# Defaults are where build/fetch-sources.sh puts them.
GCC_SRC=${GCC_SRC:-$REPO/build/src/gcc-10.2.0}
GCCSDK_SRC=${GCCSDK_SRC:-$REPO/build/src/riscos-gccsdk}
[ -d "$GCC_SRC/config" ] || { echo "no GCC source at $GCC_SRC (run build/fetch-sources.sh)" >&2; exit 1; }
[ -f "$GCCSDK_SRC/autobuilder/develop/gcc/libtool.m4.p" ] || { echo "no GCCSDK at $GCCSDK_SRC (run build/fetch-sources.sh)" >&2; exit 1; }
GCCSDK_ENV=${GCCSDK_ENV:-$HOME/gccsdk/env}
CFLAGS=${CFLAGS:-"-g -O2 -fstack-clash-protection"}
T=arm-riscos-gnueabihf
W=$REPO/build/work
# The debug information records the directory each object was compiled in
# (DW_AT_comp_dir, here $W/build) and where the toolchain's own headers are
# (GCCSDK_ENV, often under the home directory). Map both to fixed names so
# the library doesn't carry paths from the machine it was built on, and
# builds from different places give the same debug information. Added even
# when CFLAGS is given.
CFLAGS="$CFLAGS -fdebug-prefix-map=$W=/riscos-unixlib -fdebug-prefix-map=$GCCSDK_ENV=/gccsdk-env"
export PATH="$GCCSDK_ENV/bin:$PATH"
command -v $T-gcc >/dev/null || { echo "no $T-gcc in $GCCSDK_ENV/bin" >&2; exit 1; }

rm -rf "$W"; mkdir -p "$W/src"
# GCC's top level (files only) plus config/, as UnixLib's configure expects
find "$GCC_SRC" -maxdepth 1 -type f -exec cp {} "$W/src/" \;
cp -r "$GCC_SRC/config" "$W/src/config"
( cd "$W/src" && patch -p0 -s < "$GCCSDK_SRC/autobuilder/develop/gcc/libtool.m4.p" )
cp -r "$REPO/libunixlib" "$W/src/libunixlib"

# = GCCSDK's reconf-libunixlib
( cd "$W/src" && ./libunixlib/gen-auto.pl )
( cd "$W/src/libunixlib" && ACLOCAL='aclocal-1.11 -I .. -I ../config' \
    AUTOMAKE=automake-1.11 AUTOCONF=autoconf2.69 AUTOHEADER=autoheader2.69 \
    LIBTOOLIZE=true autoreconf2.69 >/dev/null )

mkdir -p "$W/build"; cd "$W/build"
CC=$T-gcc AR=$T-ar RANLIB=$T-ranlib CFLAGS="$CFLAGS" \
  ../src/libunixlib/configure --host=$T --target=$T \
    --build="$(gcc -dumpmachine)" --prefix="$GCCSDK_ENV" \
    --disable-shared --enable-static --disable-multilib >configure.log
make -j"$(nproc)" >make.log 2>&1 || { tail -40 make.log; exit 1; }
ls -l .libs/libunixlib.a
# No trace of the build machine's paths in what we ship.
if strings -a .libs/libunixlib.a pthticker | grep -F -q -e "$REPO" -e "$HOME"; then
  echo "libunixlib.a or pthticker contains a build path:" >&2
  strings -a .libs/libunixlib.a pthticker | grep -F -e "$REPO" -e "$HOME" | sort | uniq -c >&2
  exit 1
fi
# Refuse a library built from mismatched objects (see tools/check-lib.sh).
GCCSDK_ENV="$GCCSDK_ENV" "$REPO/tools/check-lib.sh" "$W/build/.libs/libunixlib.a"
if [ "$INSTALL" = yes ]; then
  cp .libs/libunixlib.a "$GCCSDK_ENV/$T/lib/libunixlib.a"
  # UnixLib's own headers (a few change, e.g. fdatasync in unistd.h)
  cp -r "$REPO/libunixlib/include/." "$GCCSDK_ENV/$T/include/"
  echo "installed libunixlib.a and headers into $GCCSDK_ENV/$T"
fi
