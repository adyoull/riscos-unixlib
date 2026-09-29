#!/usr/bin/env python3
"""Run library functions that call SWIs in an ARM emulator (Unicorn).

Links tests/emu/swi_stub.c with the built libunixlib.a and runs the real
machine code, with the SWIs faked the way RISC OS behaves:

  - __standard_time (ctime, asctime and their _r versions):
    Territory_ConvertDateAndTime returns R2 = bytes left in the buffer, and
    the function must still return the buffer.  (It returned R2: Warzone
    2100's ctime() gave &27 and crashed in strlen.)
  - __fsread (read() on a RISC OS file) into a buffer in a stack whose pages
    aren't mapped yet: every page of the buffer that is in the stack must
    be touched before OS_GBPB writes to it, because a SWI writing to an
    unmapped stack page aborts in SVC mode.  A buffer outside any stack
    must not be touched, and OS_GBPB's R2 output must not upset anything.

  tests/emu/swi_test.py [build dir]   (default build/work/build)

Needs the cross compiler (GCCSDK_ENV) and the Python module unicorn.
tests/check.sh skips it without them.
"""
import os
import struct
import subprocess
import sys
import tempfile

from unicorn import (Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_INTR,
                     UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE)
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R12,
                               UC_ARM_REG_SP,
                               UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR,
                               UC_ARM_REG_FP)

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "build/work/build")
ENV = os.environ.get("GCCSDK_ENV", os.path.expanduser("~/gccsdk/env"))
TOOL = os.path.join(ENV, "bin/arm-riscos-gnueabihf-")

X = 0x20000
VFLAG = 1 << 28
OS_GBPB = 0x0C
Territory_ConvertDateAndTime = 0x4304B
ARMEABISupport_StackOp = 0x59D02
OS_CallASWIR12 = 0x71     # what _swix uses: SWI number in R12

LOAD = 0x100000          # -Ttext of the test image
RET = 0xFFF0             # "return address": emulation stops there
CSTACK = 0x800000        # the emulator's own (mapped) stack, 64KB below
FAKESTACK = 0x2000000    # a "thread stack" with pages mapped on demand
FAKESTACK_PAGES = 16
HEAP = 0x3000000         # a buffer that isn't in any stack
ERRBLK = 0x3100000       # error block for faked errors
PAGE = 4096

DATE = b"Tue Sep 29 18:23:49 2026"

fails = 0
checks = 0


def check(cond, what):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        print("FAIL:", what)


def link():
    out = os.path.join(tempfile.mkdtemp(), "swi.elf")
    subprocess.run([TOOL + "gcc", "-O2", "-static", "-nostdlib", "-nostartfiles",
                    "-Wl,-Ttext=0x%x" % LOAD, "-Wl,-e,t_fsread",
                    "-I", os.path.join(REPO, "libunixlib/incl-local"),
                    "-isystem", os.path.join(REPO, "libunixlib/include"),
                    os.path.join(REPO, "tests/emu/swi_stub.c"),
                    "-L", os.path.join(BUILD, ".libs"), "-lunixlib", "-lgcc",
                    "-o", out], check=True)
    return out


def symbols(elf):
    out = subprocess.run([TOOL + "nm", elf], check=True, capture_output=True,
                         text=True).stdout
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    return syms


def segments(elf):
    """PT_LOAD segments of a 32-bit little-endian ELF: (vaddr, data, memsz)."""
    d = open(elf, "rb").read()
    phoff, = struct.unpack_from("<I", d, 28)
    phentsize, phnum = struct.unpack_from("<HH", d, 42)
    segs = []
    for i in range(phnum):
        p_type, p_offset, p_vaddr, _, p_filesz, p_memsz = struct.unpack_from(
            "<IIIIII", d, phoff + i * phentsize)
        if p_type == 1:
            segs.append((p_vaddr, d[p_offset:p_offset + p_filesz], p_memsz))
    return segs


class Machine:
    def __init__(self, elf):
        self.uc = uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        top = 0
        for vaddr, data, memsz in segments(elf):
            top = max(top, vaddr + memsz)
        size = (top - LOAD + 0xFFFFF) & ~0xFFFFF
        uc.mem_map(LOAD, size)
        for vaddr, data, memsz in segments(elf):
            uc.mem_write(vaddr, data)
        uc.mem_map(0, 0x10000)                       # RET page
        uc.mem_map(CSTACK - 0x10000, 0x10000)
        uc.mem_map(FAKESTACK, FAKESTACK_PAGES * PAGE)
        uc.mem_map(HEAP, 0x10000)
        uc.mem_map(ERRBLK, PAGE)
        uc.mem_write(ERRBLK, struct.pack("<I", 0x1E6) + b"Not in a stack\0")
        uc.hook_add(UC_HOOK_INTR, self.swi)
        uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, self.access,
                    begin=FAKESTACK, end=FAKESTACK + FAKESTACK_PAGES * PAGE - 1)
        self.mapped = set()          # fake-stack pages touched from USR mode
        self.heap_touched = False
        self.stackops = []
        self.gbpb = []               # (buffer, length, all pages mapped?)
        self.bytes_left = None

    def access(self, uc, access, addr, size, value, data):
        self.mapped.add((addr - FAKESTACK) // PAGE)

    def reg(self, r, v=None):
        if v is None:
            return self.uc.reg_read(r)
        self.uc.reg_write(r, v & 0xFFFFFFFF)

    def set_v(self, on):
        cpsr = self.reg(UC_ARM_REG_CPSR)
        self.reg(UC_ARM_REG_CPSR, (cpsr | VFLAG) if on else (cpsr & ~VFLAG))

    def swi(self, uc, intno, data):
        pc = self.reg(UC_ARM_REG_PC)
        num = struct.unpack("<I", uc.mem_read(pc - 4, 4))[0] & 0xFFFFFF
        num &= ~X
        if num == OS_CallASWIR12:
            num = self.reg(UC_ARM_REG_R12) & ~X
        self.set_v(False)
        r0, r1, r2, r3 = (self.reg(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1,
                                                 UC_ARM_REG_R2, UC_ARM_REG_R3))
        if num == Territory_ConvertDateAndTime:
            # R0 = buffer, R1 = terminator, R2 = bytes left; R3/R4 changed
            # too, to make sure nothing relies on them.
            uc.mem_write(r2, DATE + b"\0")
            self.bytes_left = r3 - len(DATE) - 1
            self.reg(UC_ARM_REG_R0, r2)
            self.reg(UC_ARM_REG_R1, r2 + len(DATE))
            self.reg(UC_ARM_REG_R2, self.bytes_left)
            self.reg(UC_ARM_REG_R3, 0xDEAD0003)
            self.reg(UC_ARM_REG_R4, 0xDEAD0004)
        elif num == OS_GBPB and r0 == 4:
            # R1 = handle, R2 = buffer, R3 = count.  A write to a fake-stack
            # page nobody has touched yet would abort in SVC mode on RISC OS.
            ok = True
            for a in range(r2, r2 + r3):
                if FAKESTACK <= a < FAKESTACK + FAKESTACK_PAGES * PAGE:
                    if (a - FAKESTACK) // PAGE not in self.mapped:
                        ok = False
            self.gbpb.append((r2, r3, ok))
            before = set(self.mapped)
            uc.mem_write(r2, bytes((i & 0xFF) for i in range(r3)))
            self.mapped = before     # the SWI's own writes don't count
            self.reg(UC_ARM_REG_R2, r2 + r3)
            self.reg(UC_ARM_REG_R3, 0)
            self.reg(UC_ARM_REG_R4, 0x1234)
        elif num == ARMEABISupport_StackOp:
            self.stackops.append((r0, r1))
            if r0 == 2:              # get stack: handle for an address
                if FAKESTACK <= r1 < FAKESTACK + FAKESTACK_PAGES * PAGE:
                    self.reg(UC_ARM_REG_R1, 0x5A5A)
                else:
                    self.reg(UC_ARM_REG_R0, ERRBLK)
                    self.reg(UC_ARM_REG_R1, 0)
                    self.set_v(True)
            elif r0 == 3:            # bounds: base (above guard), top
                # the first page is a guard page
                self.reg(UC_ARM_REG_R1, FAKESTACK + PAGE)
                self.reg(UC_ARM_REG_R2, FAKESTACK + FAKESTACK_PAGES * PAGE)
            else:
                raise RuntimeError("StackOp %d" % r0)
        else:
            raise RuntimeError("unexpected SWI &%X at &%X" % (num, pc - 4))

    def call(self, addr, *args):
        uc = self.uc
        for r, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                         UC_ARM_REG_R3), args):
            self.reg(r, v)
        self.reg(UC_ARM_REG_SP, CSTACK - 0x100)
        self.reg(UC_ARM_REG_FP, CSTACK - 0x100)
        self.reg(UC_ARM_REG_LR, RET)
        uc.emu_start(addr, RET, count=2000000)
        return self.reg(UC_ARM_REG_R0)


def main():
    try:
        elf = link()
    except (OSError, subprocess.CalledProcessError) as e:
        print("can't link the test image:", e)
        return 1
    syms = symbols(elf)
    for s in ("t_standard_time", "t_fsread", "__standard_time", "__fsread"):
        check(s in syms, "symbol %s" % s)

    # --- __standard_time, caller's buffer (ctime_r / asctime_r) ---
    m = Machine(elf)
    buf = HEAP + 0x100
    ret = m.call(syms["t_standard_time"], buf)
    check(ret == buf, "ctime_r: returns the caller's buffer (got &%X, "
          "bytes left was %s)" % (ret, m.bytes_left))
    text = bytes(m.uc.mem_read(buf, len(DATE) + 2))
    check(text == DATE + b"\n\0", "ctime_r: text %r" % text)

    # --- __standard_time, its static buffer (ctime / asctime) ---
    m = Machine(elf)
    ret = m.call(syms["t_standard_time"], 0)
    check(LOAD <= ret < HEAP, "ctime: returns the static buffer (got &%X)" % ret)
    check(ret != m.bytes_left, "ctime: not the 'bytes left' value")
    if LOAD <= ret < HEAP:
        text = bytes(m.uc.mem_read(ret, len(DATE) + 2))
        check(text == DATE + b"\n\0", "ctime: text %r" % text)

    # --- __fsread into a stack buffer with unmapped pages ---
    m = Machine(elf)
    start = FAKESTACK + 3 * PAGE + 100       # spans pages 3..6
    n = 3 * PAGE + 50
    got = m.call(syms["t_fsread"], start, n)
    check(got == n, "read into stack: returns %d (want %d)" % (got, n))
    check(len(m.gbpb) == 1 and m.gbpb[0][:2] == (start, n),
          "read into stack: one OS_GBPB for the whole buffer")
    check(m.gbpb and m.gbpb[0][2],
          "read into stack: every page mapped before OS_GBPB (mapped %s)"
          % sorted(m.mapped))
    check(m.mapped == {3, 4, 5, 6},
          "read into stack: touched only the buffer's pages (%s)" % sorted(m.mapped))
    data = bytes(m.uc.mem_read(start, n))
    check(data == bytes((i & 0xFF) for i in range(n)),
          "read into stack: data arrived")

    # --- __fsread starting in the guard page: clamped to the stack base ---
    m = Machine(elf)
    start = FAKESTACK + PAGE - 16
    got = m.call(syms["t_fsread"], start, 64)
    check(0 not in m.mapped, "guard page not touched (%s)" % sorted(m.mapped))
    check(1 in m.mapped, "page above the guard touched")

    # --- __fsread into a heap buffer: no stack pages touched ---
    m = Machine(elf)
    got = m.call(syms["t_fsread"], HEAP + 8, 5000)
    check(got == 5000, "read into heap: returns %d" % got)
    check(not m.mapped, "read into heap: fake stack untouched")
    check(all(op[0] == 2 for op in m.stackops),
          "read into heap: only asked whether it's a stack (%s)" % m.stackops)

    # --- zero-length read: no SWIs besides OS_GBPB ---
    m = Machine(elf)
    got = m.call(syms["t_fsread"], FAKESTACK + 5 * PAGE, 0)
    check(got == 0 and not m.stackops, "zero-length read: no StackOp")

    print("swi_test: %d checks, %d failed" % (checks, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except UcError as e:
        print("FAIL: emulator error:", e)
        sys.exit(1)
