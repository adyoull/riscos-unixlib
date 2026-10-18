#!/bin/sh -e
# Host test for the re-entrant group and password lookups (getgrgid_r,
# getgrnam_r, getpwuid_r, getpwnam_r), with /etc/group and /etc/passwd
# redirected to files the test writes.
cd "$(dirname "$0")"
mkdir -p out
L=../../../libunixlib
gcc -O1 -g -w -Ifake -include fake/shim.h -c $L/grp/getgrgid_r.c -o out/getgrgid_r.o
gcc -O1 -g -w -Ifake -include fake/shim.h -c $L/grp/getgrnam_r.c -o out/getgrnam_r.o
gcc -O1 -g -w -Ifake -include fake/shim.h -c $L/grp/grpread.c -o out/grpread.o
gcc -O1 -g -w -Ifake -include fake/shim.h -c $L/pwd/getpwuid_r.c -o out/getpwuid_r.o
gcc -O1 -g -w -Ifake -include fake/shim.h -c $L/pwd/getpwnam_r.c -o out/getpwnam_r.o
gcc -O1 -g -w -Ifake -include fake/shim.h -c $L/pwd/pwdread.c -o out/pwdread.o
gcc -O1 -g -Wall -Wno-nonnull -Ifake test_getpwgr.c out/*.o -o out/test_getpwgr
./out/test_getpwgr
