#!/bin/sh -e
# List a shared library's exported symbols, one per line, sorted:
#   NAME TYPE SIZE      (SIZE only for OBJECTs; FUNC sizes are code, free to change)
# Usage: tools/abi-dynsyms.sh LIB.so [READELF]
LIB=$1
READELF=${2:-arm-riscos-gnueabihf-readelf}
TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT
# readelf on its own, so that a failure stops the script (in a pipeline it
# wouldn't, and an empty list would pass every check).
"$READELF" --dyn-syms -W "$LIB" > "$TMP"
[ -s "$TMP" ] || { echo "$0: readelf gave nothing for $LIB" >&2; exit 1; }
awk 'NR > 3 && $7 != "UND" && $5 != "LOCAL" && $8 != "" {
       size = ($4 == "OBJECT" || $4 == "TLS") ? $3 : "-"
       print $8, $4, size }' "$TMP" | LC_ALL=C sort -u
