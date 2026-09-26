#!/bin/bash -e
# Build libunixlib.a (and optionally the shared library) from this repo's
# libunixlib/ with an installed GCCSDK GCC 10 cross compiler, without
# rebuilding GCC.
#
#   GCC_SRC=<gcc-10.2.0 source>        top-level files (config/, libtool.m4,
#                                      ltmain.sh, config-ml.in ...) are needed
#   GCCSDK_SRC=<riscos-gccsdk checkout> for autobuilder/develop/gcc/libtool.m4.p
#   GCCSDK_ENV=<installed env>         default ~/gccsdk/env
#   CFLAGS                             default "-g -O2 -fstack-clash-protection"
#   INSTALL=yes                        also copy libunixlib.a into GCCSDK_ENV
#
# Needs autoconf2.69, automake 1.11 (aclocal-1.11/automake-1.11), perl.
# Output: build/work/build/.libs/libunixlib.a
REPO=$(cd "$(dirname "$0")/.." && pwd)
: "${GCC_SRC:?set GCC_SRC to a gcc-10.2.0 source tree}"
: "${GCCSDK_SRC:?set GCCSDK_SRC to a riscos-gccsdk checkout}"
GCCSDK_ENV=${GCCSDK_ENV:-$HOME/gccsdk/env}
CFLAGS=${CFLAGS:-"-g -O2 -fstack-clash-protection"}
T=arm-riscos-gnueabihf
W=$REPO/build/work
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
if [ "$INSTALL" = yes ]; then
  cp .libs/libunixlib.a "$GCCSDK_ENV/$T/lib/libunixlib.a"
  echo "installed into $GCCSDK_ENV/$T/lib"
fi
