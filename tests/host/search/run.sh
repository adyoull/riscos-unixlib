#!/bin/sh -e
# Host test for C8: the string searches against their old versions (git).
# -funsigned-char: plain char is unsigned on ARM, as there.
cd "$(dirname "$0")"
R=../../..
mkdir -p out/old out/new
for f in strrchr strstr strspn strcspn strpbrk memmem; do
  git -C $R --no-optional-locks show 2df00bd:libunixlib/string/$f.c > out/old/$f.c
  cp $R/libunixlib/string/$f.c out/new/$f.c
done
cp $R/libunixlib/string/strchrnul.c out/new/strchrnul.c
# The new versions call UnixLib's own strchr, memchr and memcmp, as on RISC OS.
mkdir -p out/ul
for f in strchr memchr memcmp; do cp $R/libunixlib/string/$f.c out/ul/$f.c; done
CF="-O2 -funsigned-char -fno-builtin -U_FORTIFY_SOURCE -D_GNU_SOURCE -I$R/libunixlib/incl-local -Dstrong_alias(a,b)= -Dweak_alias(a,b)="
UL="-Dstrchr=ul_strchr -Dmemchr=ul_memchr -Dmemcmp=ul_memcmp -Dindex=ul_index -Dbcmp=ul_bcmp"
REN() { echo "-Dstrrchr=$1strrchr -Dstrstr=$1strstr -Dstrspn=$1strspn -Dstrcspn=$1strcspn -Dstrpbrk=$1strpbrk -Dmemmem=$1memmem -Drindex=$1rindex"; }
objs=
for v in old new ul; do
  X=; [ $v = old ] || X="$UL"
  for f in out/$v/*.c; do
    gcc $CF $(REN ${v}_) $X -include string.h -c $f -o ${f%.c}.o
    # strchrnul.c #undefs its own name, so rename the symbol instead.
    objcopy --redefine-sym __strchrnul=${v}___strchrnul ${f%.c}.o
    objs="$objs ${f%.c}.o"
  done
done
gcc -O1 -funsigned-char -o out/test_search test_search.c $objs
./out/test_search
