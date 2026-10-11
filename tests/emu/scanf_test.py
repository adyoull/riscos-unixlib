#!/usr/bin/env python3
"""scanf length modifiers on the target, in an ARM emulator (Unicorn).

Links tests/emu/scanf_stub.c with the built libunixlib.a and runs sscanf
with ll, hh, j, z, t, q, %lln, %hhn, %Lf on the real code (32-bit long),
into fields pre-filled with 0x5A, so a store of the wrong size shows: before
5.0.3.4 %lld stored only the low word, %hhd two bytes, %Lf a float.  Also
%llp must store a 4-byte pointer, and %Ld an int, as before (the peer
review of C3).  The test idea and first version are the peer reviewer's.

  tests/emu/scanf_test.py [build dir]   (default build/work/build)
"""
import os
import struct
import subprocess
import sys
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_INTR
from unicorn.arm_const import (UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_LR,
                               UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC)

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "build/work/build")
TOOL = os.path.join(os.environ.get("GCCSDK_ENV", os.path.expanduser("~/gccsdk/env")),
                    "bin/arm-riscos-gnueabihf-")
LOAD = 0x100000
fails = checks = 0


def check(cond, what):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        print("FAIL:", what)


def main():
    out = os.path.join(tempfile.mkdtemp(), "scanf.elf")
    subprocess.run([TOOL + "gcc", "-O2", "-static", "-nostdlib", "-nostartfiles",
                    "-Wl,-Ttext=0x%x" % LOAD, "-Wl,-e,t_main",
                    "-isystem", os.path.join(REPO, "libunixlib/include"),
                    os.path.join(REPO, "tests/emu/scanf_stub.c"),
                    "-L", os.path.join(BUILD, ".libs"), "-lunixlib", "-lgcc",
                    "-o", out], check=True)
    d = open(out, "rb").read()
    phoff, = struct.unpack_from("<I", d, 28)
    phentsize, phnum = struct.unpack_from("<HH", d, 42)
    segs = []
    for i in range(phnum):
        t, off, va, _, fs, ms = struct.unpack_from("<IIIIII", d, phoff + i * phentsize)
        if t == 1:
            segs.append((va, d[off:off + fs], ms))
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    lo = min(v for v, _, _ in segs) & ~0xFFFFF
    top = max(v + m for v, _, m in segs)
    uc.mem_map(lo, (top - lo + 0xFFFFF) & ~0xFFFFF)
    for v, b, _ in segs:
        uc.mem_write(v, b)
    uc.mem_map(0, 0x10000)
    uc.mem_map(0x7F0000, 0x10000)
    uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))
    uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    swis = []

    def swi(uc, intno, data):
        swis.append(uc.reg_read(UC_ARM_REG_PC))
        uc.emu_stop()
    uc.hook_add(UC_HOOK_INTR, swi)
    syms = {}
    for line in subprocess.run([TOOL + "nm", out], check=True, capture_output=True,
                               text=True).stdout.splitlines():
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    uc.reg_write(UC_ARM_REG_SP, 0x800000)
    uc.reg_write(UC_ARM_REG_LR, 0xFFF0)
    uc.emu_start(syms["t_main"], 0xFFF0, count=50000000)
    check(not swis, "no SWI called (%s)" % swis)
    R = uc.mem_read(syms["R"], 160)
    r = struct.unpack_from("<16i", R, 0)
    ll = struct.unpack_from("<6q", R, 64)
    hh = R[112:116]
    s = struct.unpack_from("<2h", R, 116)
    i = struct.unpack_from("<4i", R, 120)
    ld, = struct.unpack_from("<d", R, 136)
    pp = struct.unpack_from("<2I", R, 144)
    guard = struct.unpack_from("<2I", R, 152)
    check(r[0] == 1 and ll[0] == -9000000000, "%%lld -9000000000 (%d)" % ll[0])
    check(r[1] == 1 and ll[1] == -1, "%%llx ffffffffffffffff (%x)" % (ll[1] & (2**64 - 1)))
    check(r[2] == 1 and hh[0] == 0xFB and hh[1] == 0x5A, "%%hhd -5, next byte untouched (%s)" % hh.hex())
    check(r[3] == 1 and ll[2] == 123456789012, "%%jd (%d)" % ll[2])
    check(r[4] == 1 and i[0] == 4096, "%%zu (%d)" % i[0])
    check(r[5] == 1 and i[1] == -77, "%%td (%d)" % i[1])
    check(r[6] == 1 and ll[3] == 8000000000, "%%qd (%d)" % ll[3])
    check(ll[4] == 5, "%%lln (%d)" % ll[4])
    check(hh[2] == 3 and hh[3] == 0x5A, "%%hhn, next byte untouched (%s)" % hh.hex())
    check(r[9] == 2 and i[3] == 7 and s[0] == 8, "%%ld %%hd unchanged")
    check(r[10] == 1 and ld == 1.5, "%%Lf (%r)" % ld)
    check(r[11] == 1 and pp[1] == 0x77 and guard[0] == 0x5A5A5A5A,
          "%%llp stores a 4-byte pointer (%x, guard %x)" % (pp[1], guard[0]))
    check(r[12] == 1 and i[2] == 5, "%%Ld stores an int, as before (%d)" % i[2])
    print("scanf_test: %d checks, %d failed" % (checks, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
