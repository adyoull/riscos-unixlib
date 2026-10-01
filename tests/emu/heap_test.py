#!/usr/bin/env python3
"""The heap across several dynamic areas, in an ARM emulator (Unicorn).

Links tests/emu/heap_stub.c with the built libunixlib.a and runs the real
malloc/free/realloc (stdlib/alloc.c) and heap sbrk (sys/brk.c), with
OS_DynamicArea and OS_ChangeDynamicArea faked the way RISC OS 5 behaved
for the OpenTTD port: an area asked for with a large maximum gets a
smaller one (here 1MB instead of 128MB), while a maximum just a little
over that is given as asked.  Checks:

  - allocations past the first area's maximum carry on in new areas
    ("<name> 2", "<name> 3"), including one placed below the first;
  - every block is usable and no two overlap;
  - one block bigger than an area's usual maximum gets an area of its own
    when RISC OS allows it, and fails cleanly (no area left behind) when
    it doesn't;
  - malloc never falls back to mmap (ARMEABISupport) with a heap in
    dynamic areas;
  - __dynamic_area_extra_exit removes every area but the current one
    (which __dynamic_area_exit removes itself).

  tests/emu/heap_test.py [build dir]   (default build/work/build)
"""
import os
import struct
import subprocess
import sys
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_INTR
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5,
                               UC_ARM_REG_R8, UC_ARM_REG_R12, UC_ARM_REG_SP,
                               UC_ARM_REG_FP, UC_ARM_REG_LR, UC_ARM_REG_PC,
                               UC_ARM_REG_CPSR, UC_ARM_REG_C1_C0_2,
                               UC_ARM_REG_FPEXC)

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "build/work/build")
ENV = os.environ.get("GCCSDK_ENV", os.path.expanduser("~/gccsdk/env"))
TOOL = os.path.join(ENV, "bin/arm-riscos-gnueabihf-")

X = 0x20000
VFLAG = 1 << 28
OS_ChangeDynamicArea = 0x2A
OS_DynamicArea = 0x66
OS_CallASWIR12 = 0x71

LOAD = 0x100000
RET = 0xFFF0
CSTACK = 0x800000
ERRBLK = 0x900000
NAMES = 0x901000
MB = 1 << 20
CAP = 1 * MB             # what a "large" maximum is cut down to
HONOUR = 2 * MB          # maximums up to this are given as asked
FIRST = 7
# Where new areas go: the second above the first, the third below it.
BASES = [0x20000000, 0x08000000, 0x30000000, 0x38000000, 0x40000000,
         0x48000000, 0x50000000, 0x58000000, 0x60000000, 0x68000000]

fails = 0
checks = 0


def check(cond, what):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        print("FAIL:", what)


def link():
    out = os.path.join(tempfile.mkdtemp(), "heap.elf")
    subprocess.run([TOOL + "gcc", "-O2", "-static", "-nostdlib", "-nostartfiles",
                    "-Wl,-Ttext=0x%x" % LOAD, "-Wl,-e,t_malloc",
                    "-I", os.path.join(REPO, "libunixlib/incl-local"),
                    "-isystem", os.path.join(REPO, "libunixlib/include"),
                    os.path.join(REPO, "tests/emu/heap_stub.c"),
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


class Area:
    def __init__(self, num, base, maximum, name):
        self.num, self.base, self.max, self.name = num, base, maximum, name
        self.size = 0
        self.deleted = False


class Machine:
    def __init__(self, elf):
        self.uc = uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        top = 0
        for vaddr, data, memsz in segments(elf):
            top = max(top, vaddr + memsz)
        uc.mem_map(LOAD, (top - LOAD + 0xFFFFF) & ~0xFFFFF)
        for vaddr, data, memsz in segments(elf):
            uc.mem_write(vaddr, data)
        uc.mem_map(0, 0x10000)
        uc.mem_map(CSTACK - 0x40000, 0x40000)
        uc.mem_map(ERRBLK, 0x2000)
        uc.mem_write(ERRBLK, struct.pack("<I", 0x1E6) + b"Area full\0")
        uc.hook_add(UC_HOOK_INTR, self.swi)
        # VFP/NEON on (memcpy uses it)
        uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))
        uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        self.areas = {}
        self.ram = None          # free memory left (None: plenty)
        self.refuse_big = False  # OS_DynamicArea 0 errors above HONOUR
        self.counts = {}
        self.unexpected = []
        self.next_base = 0
        self.add_area(FIRST, 0x10000000, CAP, b"Test Heap")

    def add_area(self, num, base, maximum, name):
        a = Area(num, base, maximum, name)
        self.areas[num] = a
        self.uc.mem_map(base, (maximum + 0xFFFF) & ~0xFFFF)
        return a

    def reg(self, r, v=None):
        if v is None:
            return self.uc.reg_read(r)
        self.uc.reg_write(r, v & 0xFFFFFFFF)

    def fail_swi(self):
        self.reg(UC_ARM_REG_R0, ERRBLK)
        self.reg(UC_ARM_REG_CPSR, self.reg(UC_ARM_REG_CPSR) | VFLAG)

    def swi(self, uc, intno, data):
        pc = self.reg(UC_ARM_REG_PC)
        num = struct.unpack("<I", uc.mem_read(pc - 4, 4))[0] & 0xFFFFFF
        num &= ~X
        if num == OS_CallASWIR12:
            num = self.reg(UC_ARM_REG_R12) & ~X
        self.reg(UC_ARM_REG_CPSR, self.reg(UC_ARM_REG_CPSR) & ~VFLAG)
        r = [self.reg(x) for x in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                                   UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5)]
        key = (num, r[0]) if num == OS_DynamicArea else num
        self.counts[key] = self.counts.get(key, 0) + 1
        if num == OS_ChangeDynamicArea:
            a = self.areas.get(r[0])
            delta = struct.unpack("<i", struct.pack("<I", r[1]))[0]
            if (a is None or a.deleted or a.size + delta > a.max
                    or a.size + delta < 0
                    or (self.ram is not None and delta > self.ram)):
                self.reg(UC_ARM_REG_R1, 0)
                self.fail_swi()
                return
            a.size += delta
            if self.ram is not None:
                self.ram -= delta
            self.reg(UC_ARM_REG_R1, abs(delta))
        elif num == OS_DynamicArea and r[0] == 0:
            want = r[5]
            if self.refuse_big and want > HONOUR:
                self.fail_swi()
                return
            got = want if want <= HONOUR else CAP
            name = bytes(uc.mem_read(self.reg(UC_ARM_REG_R8), 40)).split(b"\0")[0]
            n = 100 + len(self.areas)
            base = (BASES[self.next_base] if self.next_base < len(BASES)
                    else 0x70000000 + (self.next_base - len(BASES)) * 0x400000)
            a = self.add_area(n, base, got, name)
            self.next_base += 1
            self.reg(UC_ARM_REG_R1, n)
            self.reg(UC_ARM_REG_R3, a.base)
            self.reg(UC_ARM_REG_R5, got)
        elif num == OS_DynamicArea and r[0] == 1:
            a = self.areas.get(r[1])
            if a is None or a.deleted:
                self.fail_swi()
                return
            a.deleted = True
        elif num == OS_DynamicArea and r[0] == 2:
            a = self.areas.get(r[1])
            if a is None or a.deleted:
                self.fail_swi()
                return
            uc.mem_write(NAMES, a.name + b"\0")
            self.reg(UC_ARM_REG_R2, a.size)
            self.reg(UC_ARM_REG_R3, a.base)
            self.reg(UC_ARM_REG_R4, 0x80)
            self.reg(UC_ARM_REG_R5, a.max)
            self.reg(UC_ARM_REG_R8, NAMES)
        else:
            self.unexpected.append((num, r[0]))
            self.fail_swi()

    def call(self, addr, *args):
        for reg, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                           UC_ARM_REG_R3), args):
            self.reg(reg, v)
        self.reg(UC_ARM_REG_SP, CSTACK - 0x100)
        self.reg(UC_ARM_REG_FP, CSTACK - 0x100)
        self.reg(UC_ARM_REG_LR, RET)
        self.uc.emu_start(addr, RET, count=20000000)
        return self.reg(UC_ARM_REG_R0)

    def owner(self, addr, n):
        for a in self.areas.values():
            if a.base <= addr and addr + n <= a.base + a.size:
                return a
        return None


def main():
    try:
        elf = link()
    except (OSError, subprocess.CalledProcessError) as e:
        print("can't link the test image:", e)
        return 1
    s = symbols(elf)
    m = Machine(elf)
    m.call(s["t_init"], FIRST, 0x10000000)

    # 40 blocks of 64KB: 2.5MB, past the 1MB first area.
    blocks = []
    for i in range(40):
        p = m.call(s["t_malloc"], 64 * 1024)
        blocks.append((p, 64 * 1024))
        if p:
            m.uc.mem_write(p, bytes([i]) * 64 * 1024)
    check(all(p for p, _ in blocks), "40 x 64KB allocated (%d failed)"
          % sum(1 for p, _ in blocks if not p))
    names = sorted(a.name for a in m.areas.values())
    check(b"Test Heap 2" in names and b"Test Heap 3" in names,
          "new areas named after the first: %s" % names)
    check(all(m.owner(p, n) for p, n in blocks if p),
          "every block lies inside an area's used part")
    spans = sorted((p, p + n) for p, n in blocks if p)
    check(all(spans[i][1] <= spans[i + 1][0] for i in range(len(spans) - 1)),
          "no two blocks overlap")
    check(all(bytes(m.uc.mem_read(p, 64 * 1024)) == bytes([i]) * 64 * 1024
              for i, (p, n) in enumerate(blocks) if p),
          "every block kept its contents")
    low = m.areas.get(102)
    check(low is not None and low.base < 0x10000000 and low.size > 0,
          "an area below the first one is used too")

    # Free half, allocate again: reuse, no new area.
    for p, _ in blocks[::2]:
        m.call(s["t_free"], p)
    before = len(m.areas)
    again = [m.call(s["t_malloc"], 60 * 1024) for _ in range(10)]
    check(all(again) and len(m.areas) == before,
          "freed space reused without a new area")

    # One 1.5MB block: bigger than an area's usual maximum, but RISC OS
    # gives a maximum just that big when asked.
    live = sum(1 for a in m.areas.values() if not a.deleted)
    big = m.call(s["t_malloc"], 1536 * 1024)
    check(sum(1 for a in m.areas.values() if not a.deleted) == live + 1,
          "1.5MB block: exactly one new area (%d)"
          % (sum(1 for a in m.areas.values() if not a.deleted) - live))
    check(big and m.owner(big, 1536 * 1024), "1.5MB block in an area of its own")
    if big:
        m.uc.mem_write(big, b"\x5a" * 1536 * 1024)
        check(bytes(m.uc.mem_read(big, 1536 * 1024)) == b"\x5a" * 1536 * 1024,
              "1.5MB block usable")

    # 3MB: RISC OS won't give an area that big. Fails, nothing left behind.
    live = sum(1 for a in m.areas.values() if not a.deleted)
    huge = m.call(s["t_malloc"], 3 * MB)
    check(huge == 0, "3MB block refused (got &%X)" % huge)
    check(sum(1 for a in m.areas.values() if not a.deleted) == live,
          "no area left behind by the refused block")

    # Still working afterwards; realloc across areas keeps the data.
    p = m.call(s["t_malloc"], 1000)
    check(p != 0, "small allocation after the refusal")
    m.uc.mem_write(p, b"hello\0")
    q = m.call(s["t_realloc"], p, 200 * 1024)
    check(q and bytes(m.uc.mem_read(q, 6)) == b"hello\0", "realloc keeps the data")

    check(not m.unexpected, "no other SWIs (no mmap fallback): %s"
          % ["&%X/%d" % u for u in m.unexpected])

    # A big block while the first area's top is mostly free: the new area
    # must hold the whole block (the old top can't be merged with it).
    m2 = Machine(elf)
    m2.call(s["t_init"], FIRST, 0x10000000)
    p = m2.call(s["t_malloc"], 800 * 1024)
    m2.call(s["t_free"], p)
    big = m2.call(s["t_malloc"], 1536 * 1024)
    live = [a for a in m2.areas.values() if not a.deleted]
    check(big and len(live) == 2 and m2.owner(big, 1536 * 1024) is live[1],
          "1.5MB block with 800KB free at the top: one new area holding it "
          "(areas %s)" % [(hex(a.base), a.size) for a in live])

    # Random mallocs, frees and reallocs across many areas: contents kept,
    # no overlaps, nothing outside an area.
    import random
    rnd = random.Random(2026)
    m3 = Machine(elf)
    m3.call(s["t_init"], FIRST, 0x10000000)
    live3 = {}
    ok = True
    for step in range(400):
        op = rnd.random()
        if live3 and op < 0.35:
            p = rnd.choice(list(live3))
            n, tag = live3.pop(p)
            if bytes(m3.uc.mem_read(p, n)) != bytes([tag]) * n:
                ok = False
            m3.call(s["t_free"], p)
        elif live3 and op < 0.5:
            p = rnd.choice(list(live3))
            n, tag = live3.pop(p)
            n2 = rnd.randint(1, 100 * 1024)
            q = m3.call(s["t_realloc"], p, n2)
            if not q:
                live3[p] = (n, tag)
                continue
            if bytes(m3.uc.mem_read(q, min(n, n2))) != bytes([tag]) * min(n, n2):
                ok = False
            m3.uc.mem_write(q, bytes([tag]) * n2)
            live3[q] = (n2, tag)
        else:
            n = rnd.randint(1, 100 * 1024)
            p = m3.call(s["t_malloc"], n)
            if p:
                tag = step & 0xFF
                m3.uc.mem_write(p, bytes([tag]) * n)
                live3[p] = (n, tag)
    spans = sorted((p, p + n) for p, (n, _) in live3.items())
    check(ok and all(bytes(m3.uc.mem_read(p, n)) == bytes([t]) * n
                     for p, (n, t) in live3.items()),
          "random workload: every block kept its contents")
    check(all(spans[i][1] <= spans[i + 1][0] for i in range(len(spans) - 1))
          and all(m3.owner(p, n) for p, (n, _) in live3.items()),
          "random workload: no overlaps, all inside areas")
    check(sum(1 for a in m3.areas.values() if not a.deleted) >= 3,
          "random workload used several areas (%d)"
          % sum(1 for a in m3.areas.values() if not a.deleted))
    check(not m3.unexpected, "random workload: no other SWIs")

    # Out of memory while starting a new area: the empty area is removed
    # and the heap goes on in the old one.
    m4 = Machine(elf)
    m4.call(s["t_init"], FIRST, 0x10000000)
    m4.ram = 1024 * 1024        # all used up just as the first area fills
    got = [m4.call(s["t_malloc"], 200 * 1024) for _ in range(10)]
    live = [a for a in m4.areas.values() if not a.deleted]
    check(0 in got and all(a.size > 0 for a in live),
          "out of memory: no empty area left behind (%s)"
          % [(a.num, a.size) for a in live])
    first = [p for p in got if p and m4.owner(p, 200 * 1024)
             and m4.owner(p, 200 * 1024).num == FIRST]
    if first:
        m4.call(s["t_free"], first[0])
    check(m4.call(s["t_malloc"], 100 * 1024) != 0,
          "out of memory: freed space can be used again")

    # RISC OS refuses an area with a big maximum (older systems, little
    # address space): the request is made again with just what's needed.
    m5 = Machine(elf)
    m5.call(s["t_init"], FIRST, 0x10000000)
    m5.refuse_big = True
    got = [m5.call(s["t_malloc"], 200 * 1024) for _ in range(8)]
    check(all(got) and len([a for a in m5.areas.values() if not a.deleted]) > 1,
          "big maximum refused: carries on in areas just big enough (%d failed)"
          % got.count(0))

    # Growing within an area doesn't ask RISC OS for the area's maximum
    # every time (one OS_DynamicArea 2 per actual growth at most).
    m6 = Machine(elf)
    m6.call(s["t_init"], FIRST, 0x10000000)
    for _ in range(400):
        m6.call(s["t_malloc"], 1000)
    reads = m6.counts.get((OS_DynamicArea, 2), 0)
    grows = m6.counts.get(OS_ChangeDynamicArea, 0)
    check(reads <= grows, "OS_DynamicArea 2 only when growing (%d reads, %d "
          "grows)" % (reads, grows))

    # Exit: every area but the current one is removed.
    cur = m.call(s["t_area"])
    m.call(s["t_exit"])
    left = [a.num for a in m.areas.values() if not a.deleted]
    check(left == [cur], "at exit only the current area is left for "
          "__dynamic_area_exit (left %s, current %d)" % (left, cur))

    print("heap_test: %d checks, %d failed" % (checks, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
