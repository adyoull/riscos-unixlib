#!/bin/sh -e
# Host tests for swprintf/wcsftime in wchar/wmissing.c.  Only those two
# functions (and their helper) are compiled, renamed, from the real file.
cd "$(dirname "$0")"
mkdir -p out
sed -n '/^\/\* 2026: narrow a wide format string/,/^\/\* Byte-oriented wide stream I\/O/p' \
  ../../../libunixlib/wchar/wmissing.c > out/fmt.c
{ printf '#include <errno.h>\n#include <stdarg.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <time.h>\n#include <wchar.h>\n'
  printf '#define restrict\n#define swprintf ul_swprintf\n#define wcsftime ul_wcsftime\n'
  cat out/fmt.c; } > out/fmt_host.c
gcc -O1 -g -Wall -Wno-format-security out/fmt_host.c test_wchar.c -o out/test_wchar
./out/test_wchar
