#!/usr/bin/env python3
"""Interval timer repeat delay (signal/_signal.s ITIMER_CS), in an ARM emulator.

The SIGALRM/SIGVTALRM/SIGPROF handlers re-arm OS_CallEvery with
it_interval in centiseconds. They used to add tv_usec as if it were
centiseconds (a 100 ms interval repeated every ~17 minutes). ITIMER_CS
converts it as time/setitmr.c does: 100 * tv_sec + (tv_usec + 9999) / 10000,
with a multiply by a reciprocal (no divide instruction before ARMv7).

This takes the macro from the real _signal.s, assembles it with the cross
assembler, and runs it in Unicorn for tv_usec 0..20000, the edges and a
sample of the rest, checking the result and that a4 and sp are kept. Both
versions: UMULL (ARMv4 and later) and the subtraction loop (ARMv3).

  tests/emu/itimer_test.py
"""
import os
import random
import re
import subprocess
import sys
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R3, UC_ARM_REG_SP

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(os.environ.get("GCCSDK_ENV", os.path.expanduser("~/gccsdk/env")),
                    "bin/arm-riscos-gnueabihf-")


def main():
    src = open(os.path.join(REPO, "libunixlib/signal/_signal.s")).read()
    m = re.search(r"^\t\.macro\tITIMER_CS\n.*?^\t\.endm\n", src, re.S | re.M)
    if not m:
        print("FAIL: no ITIMER_CS macro in signal/_signal.s")
        return 1
    checks = fails = 0
    for arch in (4, 3):
        c, f = run(m.group(0), arch)
        checks += c
        fails += f
    print("itimer_test: %d checks, %d failed" % (checks, fails))
    return 1 if fails else 0


def run(macro, arch):
    d = tempfile.mkdtemp()
    with open(os.path.join(d, "t.S"), "w") as f:
        f.write("\t.syntax unified\n#define a1 r0\n#define a2 r1\n#define a3 r2\n#define a4 r3\n")
        f.write("#define __ARM_ARCH__ %d\n" % arch)
        f.write(macro)
        f.write("\t.text\nf:\n\tITIMER_CS\n\tbkpt\n")
    subprocess.run([TOOL + "gcc", "-c", "-o", os.path.join(d, "t.o"), os.path.join(d, "t.S")], check=True)
    subprocess.run([TOOL + "objcopy", "-O", "binary", "-j", ".text",
                    os.path.join(d, "t.o"), os.path.join(d, "t.bin")], check=True)
    code = open(os.path.join(d, "t.bin"), "rb").read()

    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(0x10000, 0x1000)
    uc.mem_write(0x10000, code)
    uc.mem_map(0x80000, 0x10000)
    end = 0x10000 + len(code) - 4          # the bkpt
    random.seed(1)
    usecs = list(range(0, 20001)) + [99999, 100000, 100001, 999998, 999999] \
        + random.sample(range(1000000), 2000)
    checks = fails = 0
    for sec in (0, 1, 59, 3600):
        for usec in usecs:
            uc.reg_write(UC_ARM_REG_R0, sec)
            uc.reg_write(UC_ARM_REG_R1, usec)
            uc.reg_write(UC_ARM_REG_R3, 0x5A5A5A5A)
            uc.reg_write(UC_ARM_REG_SP, 0x88000)
            uc.emu_start(0x10000, end)
            want = 100 * sec + (usec + 9999) // 10000
            got = uc.reg_read(UC_ARM_REG_R0)
            checks += 1
            if (got != want or uc.reg_read(UC_ARM_REG_R3) != 0x5A5A5A5A
                    or uc.reg_read(UC_ARM_REG_SP) != 0x88000):
                fails += 1
                if fails <= 5:
                    print("FAIL (ARMv%d): %d s %d us -> %d cs (want %d)"
                          % (arch, sec, usec, got, want))
    return checks, fails


if __name__ == "__main__":
    sys.exit(main())
