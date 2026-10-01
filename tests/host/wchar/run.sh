#!/bin/sh -e
# Host tests for swprintf/wcsftime and the wcsto* conversions in
# wchar/wmissing.c.  Only those functions (and their helpers) are compiled,
# renamed, from the real file.
cd "$(dirname "$0")"
mkdir -p out
sed -n '/^\/\* 2026: narrow a wide format string/,/^\/\* Byte-oriented wide stream I\/O/p' \
  ../../../libunixlib/wchar/wmissing.c > out/fmt.c
sed -n '/^\/\* Numeric conversions: narrow/,/^WCSTOF (wcstold/p' \
  ../../../libunixlib/wchar/wmissing.c > out/num.c
{ printf '#include <errno.h>\n#include <stdarg.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <time.h>\n#include <wchar.h>\n'
  printf '#define restrict\n#define swprintf ul_swprintf\n#define wcsftime ul_wcsftime\n'
  printf '#define wcstol ul_wcstol\n#define wcstoul ul_wcstoul\n#define wcstoll ul_wcstoll\n#define wcstoull ul_wcstoull\n'
  printf '#define wcstod ul_wcstod\n#define wcstof ul_wcstof\n#define wcstold ul_wcstold\n'
  cat out/fmt.c out/num.c; } > out/fmt_host.c
gcc -O1 -g -Wall -Wno-format-security out/fmt_host.c test_wchar.c -o out/test_wchar
./out/test_wchar
