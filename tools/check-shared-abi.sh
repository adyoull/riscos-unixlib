#!/bin/sh -e
# Check a built libunixlib.so against GCCSDK's (tools/abi/): programs and
# libraries linked against GCCSDK's libunixlib.so.5 (Iris's, for example)
# must load ours in its place. Fails if
#   - the SONAME isn't libunixlib.so.5, or it needs anything but libgcc_s.so.1;
#   - it has text relocations (SOManager would have to patch its code);
#   - a symbol GCCSDK's library exports is missing, or changed type;
#   - an exported data object changed size (copy relocations in programs
#     would then copy the wrong amount).
# New symbols are fine and only counted.
# Usage: tools/check-shared-abi.sh LIB.so   (GCCSDK_ENV for the readelf)
LIB=$1
[ -f "$LIB" ] || { echo "usage: $0 libunixlib.so" >&2; exit 1; }
LIB=$(readlink -f "$LIB")
[ -n "$GCCSDK_ENV" ] && GCCSDK_ENV=$(cd "$GCCSDK_ENV" && pwd)
cd "$(dirname "$0")/.."
READELF=arm-riscos-gnueabihf-readelf
[ -n "$GCCSDK_ENV" ] && READELF=$GCCSDK_ENV/bin/$READELF
REF=tools/abi/gccsdk-libunixlib.so.5.0.0.syms
[ -s "$REF" ] || { echo "FAIL: no reference list $REF" >&2; exit 1; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0

dyn=$("$READELF" -d "$LIB")
echo "$dyn" | grep -q 'SONAME.*\[libunixlib\.so\.5\]' ||
  { echo "FAIL: SONAME isn't libunixlib.so.5" >&2; fail=1; }
needed=$(echo "$dyn" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | tr '\n' ' ')
[ "$needed" = "libgcc_s.so.1 " ] ||
  { echo "FAIL: needs \"$needed\" (want only libgcc_s.so.1)" >&2; fail=1; }
if echo "$dyn" | grep -q 'TEXTREL'; then
  echo "FAIL: text relocations" >&2; fail=1
fi

sh tools/abi-dynsyms.sh "$LIB" "$READELF" > "$TMP/new"
[ -s "$TMP/new" ] || { echo "FAIL: no exported symbols read from $LIB" >&2; exit 1; }
# Missing, or a different type or data size: lines of REF not in new.
LC_ALL=C comm -23 "$REF" "$TMP/new" > "$TMP/gone"
if [ -s "$TMP/gone" ]; then
  echo "FAIL: exported by GCCSDK's library but missing or changed here:" >&2
  while read -r name type size; do
    now=$(awk -v n="$name" '$1 == n { print $2, $3 }' "$TMP/new")
    echo "  $name $type $size -> ${now:-missing}" >&2
  done < "$TMP/gone"
  fail=1
fi
added=$(LC_ALL=C comm -13 "$REF" "$TMP/new" | wc -l)
[ $fail = 0 ] || exit 1
echo "$LIB: ABI OK ($(wc -l < "$REF") of GCCSDK's symbols, $added more)"
