#!/bin/sh -e
# Host test for C6: strtol and friends against their old versions (git).
# Built with -m32 where the host can, so long is 32 bits as on RISC OS.
cd "$(dirname "$0")"
R=../../..
mkdir -p out/old out/new
for f in strtol strtoul strtoll strtoull; do
  git -C $R --no-optional-locks show 14e49e6:libunixlib/stdlib/$f.c > out/old/$f.c
  cp $R/libunixlib/stdlib/$f.c out/new/$f.c
done
M=
echo 'int main(void){return 0;}' > out/m32.c
gcc -m32 out/m32.c -o out/m32 2>/dev/null && M=-m32
objs=
for v in old new; do
  for f in out/$v/*.c; do
    b=$(basename $f .c)
    gcc $M -O2 -funsigned-char -fno-builtin -D$b=${v}_$b -include ctype.h -include limits.h -include errno.h -include stdlib.h -c $f -o ${f%.c}.o
    objs="$objs ${f%.c}.o"
  done
done
gcc $M -O1 -o out/test_strto test_strto.c $objs
./out/test_strto
