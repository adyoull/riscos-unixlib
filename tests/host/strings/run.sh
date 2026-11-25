#!/bin/sh -e
# Host test for C5: the string functions against their old versions (git).
# -funsigned-char: plain char is unsigned on ARM, as there.
cd "$(dirname "$0")"
R=../../..
mkdir -p out/old out/new
for f in strcmp strncmp memcmp strchr memchr stricmp strnicmp; do
  git -C $R --no-optional-locks show 14e49e6:libunixlib/string/$f.c > out/old/$f.c
  cp $R/libunixlib/string/$f.c out/new/$f.c
done
# The old memchr casts the pointer through an int: wrong on a 64-bit host.
sed -i 's/(unsigned char \*)(int) s/(unsigned char *) s/' out/old/memchr.c
CF="-O2 -funsigned-char -fno-builtin -U_FORTIFY_SOURCE -D_GNU_SOURCE -I$R/libunixlib/incl-local -Dstrong_alias(a,b)="
REN() { echo "-Dstrcmp=$1strcmp -Dstrncmp=$1strncmp -Dmemcmp=$1memcmp -Dstrchr=$1strchr -Dmemchr=$1memchr -Dstricmp=$1stricmp -Dstrnicmp=$1strnicmp -Dbcmp=$1bcmp -Dindex=$1index -Dstrcasecmp=$1strcasecmp -Dstrncasecmp=$1strncasecmp"; }
objs=
for v in old new; do
  for f in out/$v/*.c; do
    gcc $CF $(REN ${v}_) -include string.h -include strings.h -include ctype.h -c $f -o ${f%.c}.o
    objs="$objs ${f%.c}.o"
  done
done
gcc -O1 -funsigned-char -o out/test_strings test_strings.c $objs
./out/test_strings
