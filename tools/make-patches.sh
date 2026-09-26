#!/bin/sh -e
# Regenerate patches/*.diff from the git history.
#   tools/make-patches.sh          write them
#   tools/make-patches.sh --check  fail if the committed ones are out of date
#
# The paths in the diffs are GCCSDK's (gcc4/recipe/files/gcc/libunixlib/...),
# so they apply to a GCCSDK checkout with `patch -p1`.
cd "$(dirname "$0")/.."

# The first commit is the unchanged import of GCCSDK's UnixLib.
IMPORT=$(git rev-list --max-parents=0 HEAD)
# The sound work on its own (for offering to GCCSDK): from the commit
# before it to the last sound commit.
SOUND_FROM=5f334f8
SOUND_TO=14f0161

gen () {
  git diff "$1" "$2" --src-prefix=a/gcc4/recipe/files/gcc/ \
    --dst-prefix=b/gcc4/recipe/files/gcc/ -- libunixlib
}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
gen "$IMPORT" HEAD > "$tmp/unixlib-riscos.diff"
gen "$SOUND_FROM" "$SOUND_TO" > "$tmp/unixlib-sound.diff"

if [ "$1" = "--check" ]; then
  bad=0
  for f in unixlib-riscos.diff unixlib-sound.diff; do
    if ! cmp -s "$tmp/$f" "patches/$f"; then
      echo "patches/$f is out of date: run tools/make-patches.sh" >&2
      bad=1
    fi
  done
  [ $bad = 0 ] && echo "patches up to date"
  exit $bad
fi
cp "$tmp"/*.diff patches/
echo "patches/ regenerated"
