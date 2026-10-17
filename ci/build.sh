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
# The library build needs autoconf 2.69 and automake 1.11; the image has
# them from riscos-crossdev 1.8.1.
#
# It doesn't `make install`: that would only change the image's own
# toolchain, which is thrown away after the run.
set -e    # also when run as `sh ci/build.sh`
cd "$(dirname "$0")/.."

# make check compares patches/ with the git history. Run on a checkout
# owned by another user (root in a container job), git refuses the
# repository ("dubious ownership") unless it's marked safe.
git config --global --get-all safe.directory 2>/dev/null | grep -qxF "$PWD" ||
  git config --global --add safe.directory "$PWD"

if ! command -v autoconf2.69 >/dev/null || ! command -v automake-1.11 >/dev/null; then
  echo "ci/build.sh: needs autoconf2.69 and automake-1.11 (riscos-crossdev 1.8.1's image has them;" >&2
  echo "elsewhere: apt-get install autoconf2.69 automake1.11)" >&2
  exit 1
fi

make sources
make lib
make check
make riscos-tests
