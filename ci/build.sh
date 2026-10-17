#!/bin/sh -e
# The build that GitHub runs (.github/workflows/riscos-build.yml and
# riscos-deps-update.yml), in the riscos-crossdev container image named in
# .riscos-deps. From a clean checkout, with nothing put in by hand:
#
#   - fetch and check GCCSDK and the GCC source (make sources);
#   - build libunixlib.a with the image's toolchain ($RISCOS_TOOLCHAIN,
#     picked up by the Makefile) (make lib);
#   - the host, emulator, ABI and patch checks (make check);
#   - the RISC OS test programs, UnixLibTests.zip (make riscos-tests).
#
# The library build needs autoconf 2.69 and automake 1.11, which the image
# doesn't have yet: they're installed here when missing (as root, which is
# how both workflows run the image). Ubuntu 24.04 packages both.
#
# It doesn't `make install`: that would only change the image's own
# toolchain, which is thrown away after the run.
cd "$(dirname "$0")/.."

# make check compares patches/ with the git history. Run as root on a
# checkout owned by another user, git refuses the repository ("dubious
# ownership") unless it's marked safe.
git config --global --get-all safe.directory 2>/dev/null | grep -qxF "$PWD" ||
  git config --global --add safe.directory "$PWD"

if ! command -v autoconf2.69 >/dev/null || ! command -v automake-1.11 >/dev/null; then
  if [ "$(id -u)" = 0 ] && command -v apt-get >/dev/null; then
    apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
      autoconf2.69 automake1.11 perl >/dev/null
  else
    echo "ci/build.sh: needs autoconf2.69 and automake-1.11 (apt-get install autoconf2.69 automake1.11)" >&2
    exit 1
  fi
fi

make sources
make lib
make check
make riscos-tests
