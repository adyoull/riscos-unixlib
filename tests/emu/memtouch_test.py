#!/usr/bin/env python3
"""M3 and B2: memcpy, memmove, memset, mempcpy, bcopy and setjmp in an ARM
emulator (Unicorn), on the built libunixlib.a.

- Results for many sizes and alignments, overlapping both ways for memmove
  and bcopy (B2: bcopy's arguments are (src, dest, n), and it used to copy
  the wrong way).
- Blocks flush against memory that isn't there: nothing outside the block
  is read or written.
- Pages mapped in on first touch, as ARMEABISupport's stacks and mmap
  memory are, except that, as with ARMEABISupport 1.07+ and AbortTrap, a
  first touch by an instruction that moves more than 60 bytes (VLDM/VSTM,
  NEON VLD1/VST1, LDM/STM) is refused: ARMEABISupport copies at most 60.
  memcpy of 64 bytes and more, and setjmp (64 bytes of d8-d15), used to
  make such a first touch; with M3 they touch the pages with ARM loads
  first.  The previous library fails this part.

  tests/emu/memtouch_test.py [build dir]   (default build/work/build)
"""
import os
import random
import struct
import subprocess
import sys
import tempfile

from unicorn import (Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_INTR,
                     UC_HOOK_MEM_READ_UNMAPPED, UC_HOOK_MEM_WRITE_UNMAPPED)
from unicorn.arm_const import (UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_LR,
                               UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC)

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "build/work/build")
TOOL = os.path.join(os.environ.get("GCCSDK_ENV", os.path.expanduser("~/gccsdk/env")),
                    "bin/arm-riscos-gnueabihf-")
LOAD = 0x100000
RET = 0xFFF0
STACK = 0x800000
BUF = 0x30000000          # ordinary memory, 64 KB
EDGE = 0x50000000         # 16 KB with nothing either side
LAZY = 0x60000000         # 64 KB mapped a page at a time on first touch
fails = checks = 0


def check(cond, what):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        if fails <= 20:
            print("FAIL:", what)


def transfer_size(insn):
    """Bytes one ARM-state load/store instruction moves (0 if unknown)."""
    if (insn & 0xFF100000) == 0xF4000000:          # NEON VLD/VST
        if insn & (1 << 23):                       # single lane / all lanes
            return 32
        regs = {7: 1, 10: 2, 6: 3, 2: 4}.get((insn >> 8) & 0xF, 4)
        return regs * 8
    if (insn & 0x0E000E00) == 0x0C000A00:          # VFP load/store
        p, w = (insn >> 24) & 1, (insn >> 21) & 1
        if p == 1 and w == 0:                      # VLDR/VSTR
            return 8 if insn & 0x100 else 4
        return (insn & 0xFF) * 4                   # VLDM/VSTM (words)
    if (insn & 0x0E000000) == 0x08000000:          # LDM/STM
        return bin(insn & 0xFFFF).count("1") * 4
    return 4


def main():
    out = os.path.join(tempfile.mkdtemp(), "memtouch.elf")
    subprocess.run([TOOL + "gcc", "-O2", "-static", "-nostdlib", "-nostartfiles",
                    "-Wl,-Ttext=0x%x" % LOAD, "-Wl,-e,t_main",
                    "-isystem", os.path.join(REPO, "libunixlib/include"),
                    os.path.join(REPO, "tests/emu/memtouch_stub.c"),
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
    syms = {}
    for line in subprocess.run([TOOL + "nm", out], check=True, capture_output=True,
                               text=True).stdout.splitlines():
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)

    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    lo = min(v for v, _, _ in segs) & ~0xFFFFF
    top = max(v + m for v, _, m in segs)
    uc.mem_map(lo, (top - lo + 0xFFFFF) & ~0xFFFFF)
    for v, b, _ in segs:
        uc.mem_write(v, b)
    uc.mem_map(0, 0x10000)
    uc.mem_map(STACK - 0x10000, 0x10000)
    uc.mem_map(BUF, 0x10000)
    uc.mem_map(EDGE, 0x4000)
    uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))
    uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    uc.hook_add(UC_HOOK_INTR, lambda uc, n, d: uc.emu_stop())

    lazy = set()          # pages of LAZY mapped so far
    refused = []          # first touches by NEON/VFP instructions
    stray = []            # touches of memory that isn't there

    def unmapped(uc, access, addr, size, value, data):
        pc = uc.reg_read(UC_ARM_REG_PC)
        insn, = struct.unpack("<I", uc.mem_read(pc, 4))
        if LAZY <= addr < LAZY + 0x10000:
            if transfer_size(insn) > 60:
                refused.append((addr, insn))
                return False
            page = addr & ~0xFFF
            uc.mem_map(page, 0x1000)
            lazy.add(page)
            return True
        stray.append((addr, pc))
        return False
    uc.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED, unmapped)

    def call(fn, a, b, c, sp=STACK - 0x100):
        uc.reg_write(UC_ARM_REG_R0, a)
        uc.reg_write(UC_ARM_REG_R1, b)
        uc.reg_write(UC_ARM_REG_R2, c)
        uc.reg_write(UC_ARM_REG_SP, sp)
        uc.reg_write(UC_ARM_REG_LR, RET)
        try:
            uc.emu_start(syms[fn], RET, count=5000000)
        except UcError as e:
            return "error %s" % e
        return uc.reg_read(UC_ARM_REG_R0)

    def reset_lazy():
        for p in lazy:
            uc.mem_unmap(p, 0x1000)
        lazy.clear()
        del refused[:]

    rnd = random.Random(1)

    # 1. Results.
    sizes = list(range(0, 300)) + [511, 512, 1023, 4095, 4096, 4097, 5000, 9000, 20000]
    for n in sizes:
        for _ in range(3):
            so, do = rnd.randrange(64), rnd.randrange(64)
            src = rnd.randbytes(n)
            uc.mem_write(BUF, rnd.randbytes(0x10000))
            uc.mem_write(BUF + 0x100 + so, src)
            before = bytes(uc.mem_read(BUF, 0x10000))
            d = BUF + 0x8000 + do
            r = call("memcpy", d, BUF + 0x100 + so, n)
            got = bytes(uc.mem_read(BUF, 0x10000))
            want = before[:0x8000 + do] + src + before[0x8000 + do + n:]
            check(r == d and got == want, "memcpy n %d" % n)
            r = call("mempcpy", BUF + 0x8000 + do, BUF + 0x100 + so, n)
            check(r == BUF + 0x8000 + do + n, "mempcpy returns the end (n %d)" % n)
            r = call("memset", d, 0xA5, n)
            got = bytes(uc.mem_read(BUF, 0x10000))
            want = want[:0x8000 + do] + b"\xa5" * n + want[0x8000 + do + n:]
            check(r == d and got == want, "memset n %d" % n)
            # Overlapping, both ways, through memmove and bcopy.
            for fn in ("memmove", "bcopy"):
                for shift in (-7, -1, 1, 13):
                    if n > 20000:
                        continue
                    blk = rnd.randbytes(0x10000)
                    uc.mem_write(BUF, blk)
                    s = BUF + 0x4000 + so
                    t = s + shift
                    if fn == "memmove":
                        r = call(fn, t, s, n)
                    else:
                        r = call(fn, s, t, n)
                    got = bytes(uc.mem_read(BUF, 0x10000))
                    o, p = s - BUF, t - BUF
                    want = blk[:p] + blk[o:o + n] + blk[p + n:]
                    check(got == want, "%s n %d shift %d" % (fn, n, shift))
                    if fn == "memmove":
                        check(r == t, "memmove returns dest")

    # 2. Nothing outside the blocks is touched.
    for n in (1, 3, 64, 100, 4095, 4096, 4097, 8191, 0x3000):
        for at_end in (False, True):
            d = EDGE + 0x4000 - n if at_end else EDGE
            s = EDGE if at_end else EDGE + 0x4000 - n
            del stray[:]
            r1 = call("memset", d, 0, n)
            r2 = call("memcpy", d, s, n) if n <= 0x2000 else d
            r3 = call("memmove", d, s, n)
            check(not stray and r1 == d and r2 == d and r3 == d,
                  "edge n %d %s: %s %s %s %s" % (n, "end" if at_end else "start",
                                                 stray[:2], r1, r2, r3))
    del stray[:]
    for fn in ("memcpy", "memmove", "memset"):
        r = call(fn, 0, 0, 0)
        check(not stray and r == 0, "%s of 0 bytes at address 0 touches nothing" % fn)

    # 3. Pages mapped on first touch, NEON/VFP first touches refused.
    for n in (1, 16, 64, 100, 300, 4096, 5000, 9000, 20000):
        for off in (0, 4, 0xFF0, 0xFFC):
            for fn in ("memcpy", "memset", "memmove", "memcpysrc", "bcopy"):
                reset_lazy()
                uc.mem_write(BUF, rnd.randbytes(n))
                dst = LAZY + off
                if fn == "memcpy":
                    r = call("memcpy", dst, BUF, n)
                elif fn == "memset":
                    r = call("memset", dst, 0x3C, n)
                elif fn == "memmove":
                    r = call("memmove", dst, BUF, n)
                elif fn == "bcopy":
                    r = call("bcopy", BUF, dst, n)
                else:
                    r = call("memcpy", BUF + 0x8000, dst, n)
                ok = not refused and not isinstance(r, str)
                if ok and fn != "memcpysrc":
                    got = bytes(uc.mem_read(dst, n))
                    want = (b"\x3c" * n if fn == "memset"
                            else bytes(uc.mem_read(BUF, n)))
                    ok = got == want
                check(ok, "fresh pages: %s n %d at +%x: %s %s" %
                      (fn, n, off, r, ["%x %08x" % x for x in refused[:2]]))
    # setjmp's 64-byte VSTM of d8-d15 reaching into a fresh page.
    for slack in (8, 16, 40, 60, 64):
        reset_lazy()
        upper = LAZY + 0x8000
        uc.mem_map(upper, 0x1000)
        lazy.add(upper)
        buf = upper + 0x1000 - slack
        r = call("setjmp", buf, 0, 0)
        check(not refused and r == 0,
              "setjmp with its jmp_buf %d bytes before a fresh page: %s %s"
              % (slack, r, ["%x %08x" % x for x in refused[:2]]))
    # memcpy's own VPUSH onto a fresh stack page.
    for slack in (8, 12, 16, 20, 24):
        reset_lazy()
        upper = LAZY + 0x8000
        uc.mem_map(upper, 0x1000)
        lazy.add(upper)
        r = call("memcpy", BUF + 0x8000, BUF, 64, sp=upper + slack)
        check(not refused and r == BUF + 0x8000,
              "memcpy with sp %d bytes into a page, the one below fresh: %s %s"
              % (slack, r, ["%x %08x" % x for x in refused[:2]]))

    print("memtouch_test: %d checks, %d failed" % (checks, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
