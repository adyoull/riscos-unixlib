#!/bin/sh -e
# Print the commit that imported GCCSDK's UnixLib unchanged (the newest one
# whose message starts "Import UnixLib" or "Update UnixLib"). Our changes
# are everything after it.
cd "$(dirname "$0")/.."
c=$(git log --format=%H -E --grep='^(Import|Update) UnixLib' -1 HEAD)
[ -n "$c" ] || { echo "no import commit found" >&2; exit 1; }
echo "$c"
