#!/usr/bin/env python3
"""The heap across several dynamic areas, in an ARM emulator (Unicorn).

Links tests/emu/heap_stub.c with the built libunixlib.a and runs the real
malloc/free/realloc (stdlib/alloc.c) and heap sbrk (sys/brk.c), with
OS_DynamicArea and OS_ChangeDynamicArea faked the way RISC OS 5 behaved on
a Pi 4: an area's maximum is cut down (here to 1MB instead of 128MB), and
an area asked for at a given base (R3) is put there if the space is free
(tests/riscos/daprobe.c).  Checks, with areas placed one after another:

  - the heap carries on in areas made directly after each other ("<name>
    2", "<name> 3"), so it stays one range: blocks bigger than an area,
    a big block reusing a free top, random malloc/free/realloc;
  - with the space after an area taken, or fixed bases refused, it goes on
    in an area elsewhere (a new segment) and still works;
  - memory running out, a big maximum refused, no needless SWIs, no mmap
    fallback, and every area but the current one removed at exit.

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
    def __init__(self, elf, fixed=True):
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
        self.partial = False     # out of RAM: grow what's left, then error
        self.counts = {}
        self.unexpected = []
        self.next_base = 0
        self.fixed = fixed       # honour a base asked for in R3
        self.add_area(FIRST, 0x10000000, CAP, b"Test Heap")

    def add_area(self, num, base, maximum, name):
        a = Area(num, base, maximum, name)
        self.areas[num] = a
        self.uc.mem_map(base, (maximum + 0xFFFF) & ~0xFFFF)
        return a

    def free_at(self, base, maximum):
        end = base + ((maximum + 0xFFFF) & ~0xFFFF)
        return all(a.deleted or end <= a.base
                   or base >= a.base + ((a.max + 0xFFFF) & ~0xFFFF)
                   for a in self.areas.values())

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
            if (self.partial and a is not None and not a.deleted and delta > 0
                    and self.ram is not None and delta > self.ram
                    and a.size + delta <= a.max and self.ram > 0):
                part = self.ram & ~0xFFF
                a.size += part
                self.ram -= part
                self.reg(UC_ARM_REG_R1, part)
                self.fail_swi()
                return
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
            if r[3] != 0xFFFFFFFF:
                if not self.fixed or not self.free_at(r[3], got):
                    self.fail_swi()
                    return
                base = r[3]
            else:
                while True:
                    base = (BASES[self.next_base] if self.next_base < len(BASES)
                            else 0x70000000 + (self.next_base - len(BASES)) * 0x400000)
                    self.next_base += 1
                    if self.free_at(base, got):
                        break
            a = self.add_area(n, base, got, name)
            self.reg(UC_ARM_REG_R1, n)
            self.reg(UC_ARM_REG_R3, a.base)
            self.reg(UC_ARM_REG_R5, got)
        elif num == OS_DynamicArea and r[0] == 1:
            a = self.areas.get(r[1])
            if a is None or a.deleted:
                self.fail_swi()
                return
            a.deleted = True
            uc.mem_unmap(a.base, (a.max + 0xFFFF) & ~0xFFFF)
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
            if not a.deleted and a.base <= addr and addr + n <= a.base + a.size:
                return a
        return None

    def covered(self, addr, n):
        """Is [addr, addr+n) inside used parts of live areas (which may be
        next to each other)?"""
        end = addr + n
        while addr < end:
            a = self.owner(addr, 1)
            if a is None:
                return False
            addr = a.base + a.size
        return True

    def live(self):
        return sorted((a for a in self.areas.values() if not a.deleted),
                      key=lambda a: a.base)

    def one_range(self):
        """Do the live heap areas (not foreign ones) lie end to end?"""
        ar = [a for a in self.live() if a.name.startswith(b"Test Heap")]
        return all(ar[i].base + ar[i].max == ar[i + 1].base
                   for i in range(len(ar) - 1))


def main():
    try:
        elf = link()
    except (OSError, subprocess.CalledProcessError) as e:
        print("can't link the test image:", e)
        return 1
    s = symbols(elf)
    import random

    def fresh(**kw):
        m = Machine(elf, **kw)
        m.call(s["t_init"], FIRST, 0x10000000)
        return m

    def fill(m, count, size):
        out = []
        for i in range(count):
            p = m.call(s["t_malloc"], size)
            out.append(p)
            if p:
                m.uc.mem_write(p, bytes([i & 0xFF]) * size)
        return out

    def intact(m, ptrs, size):
        return all(bytes(m.uc.mem_read(p, size)) == bytes([i & 0xFF]) * size
                   for i, p in enumerate(ptrs) if p)

    def no_overlap(spans):
        spans = sorted(spans)
        return all(spans[i][1] <= spans[i + 1][0] for i in range(len(spans) - 1))

    def random_workload(m, steps, top):
        rnd = random.Random(2026)
        live, ok = {}, True
        for step in range(steps):
            op = rnd.random()
            if live and op < 0.35:
                p = rnd.choice(list(live))
                n, tag = live.pop(p)
                ok &= bytes(m.uc.mem_read(p, n)) == bytes([tag]) * n
                m.call(s["t_free"], p)
            elif live and op < 0.5:
                p = rnd.choice(list(live))
                n, tag = live.pop(p)
                n2 = rnd.randint(1, top)
                q = m.call(s["t_realloc"], p, n2)
                if not q:
                    live[p] = (n, tag)
                    continue
                k = min(n, n2)
                ok &= bytes(m.uc.mem_read(q, k)) == bytes([tag]) * k
                m.uc.mem_write(q, bytes([tag]) * n2)
                live[q] = (n2, tag)
            else:
                n = rnd.randint(1, top)
                p = m.call(s["t_malloc"], n)
                if p:
                    tag = step & 0xFF
                    m.uc.mem_write(p, bytes([tag]) * n)
                    live[p] = (n, tag)
        ok &= all(bytes(m.uc.mem_read(p, n)) == bytes([t]) * n
                  for p, (n, t) in live.items())
        return ok, live

    def exit_check(m, what):
        cur = m.call(s["t_area"])
        m.call(s["t_exit"])
        left = [a.num for a in m.live() if a.name.startswith(b"Test Heap")]
        check(left == [cur], "%s: at exit only the current area is left for "
              "__dynamic_area_exit (left %s, current %d)" % (what, left, cur))

    # ---- Areas placed one after another (RISC OS 5) ----
    m = fresh()
    blocks = fill(m, 40, 64 * 1024)
    check(all(blocks), "40 x 64KB allocated (%d failed)" % blocks.count(0))
    names = sorted(a.name for a in m.live())
    check(b"Test Heap 2" in names and b"Test Heap 3" in names,
          "new areas named after the first: %s" % names)
    check(m.one_range(), "the areas lie end to end: %s"
          % [(hex(a.base), a.max) for a in m.live()])
    check(all(m.covered(p, 64 * 1024) for p in blocks if p)
          and no_overlap((p, p + 64 * 1024) for p in blocks if p)
          and intact(m, blocks, 64 * 1024),
          "blocks inside the areas, no overlaps, contents kept")
    # A block bigger than one area: spans areas.
    big = m.call(s["t_malloc"], 2560 * 1024)
    check(big and m.covered(big, 2560 * 1024),
          "2.5MB block (areas hold 1MB) allocated across areas (&%X)" % big)
    if big:
        m.uc.mem_write(big, b"\x5a" * 2560 * 1024)
        check(bytes(m.uc.mem_read(big, 2560 * 1024)) == b"\x5a" * 2560 * 1024
              and intact(m, blocks, 64 * 1024),
              "2.5MB block usable, other blocks untouched")
    check(m.one_range(), "still one range after the big block")
    p = m.call(s["t_malloc"], 1000)
    m.uc.mem_write(p, b"hello\0")
    q = m.call(s["t_realloc"], p, 1200 * 1024)
    check(q and bytes(m.uc.mem_read(q, 6)) == b"hello\0",
          "realloc to 1.2MB keeps the data")
    check(not m.unexpected, "no other SWIs (no mmap fallback): %s"
          % ["&%X/%d" % u for u in m.unexpected])
    exit_check(m, "chained")

    # A big block while the top is mostly free: it reuses that space.
    m2 = fresh()
    p = m2.call(s["t_malloc"], 800 * 1024)
    m2.call(s["t_free"], p)
    big = m2.call(s["t_malloc"], 1536 * 1024)
    used = sum(a.size for a in m2.live())
    check(big and m2.covered(big, 1536 * 1024) and used <= 1800 * 1024,
          "1.5MB block after freeing 800KB: reuses the free top (%d KB "
          "used)" % (used // 1024))

    # Random workload, blocks up to 300KB, across many areas.
    m3 = fresh()
    ok, live = random_workload(m3, 400, 300 * 1024)
    check(ok, "random workload: every block kept its contents")
    check(no_overlap((p, p + n) for p, (n, _) in live.items())
          and all(m3.covered(p, n) for p, (n, _) in live.items()),
          "random workload: no overlaps, all inside the areas")
    check(len(m3.live()) >= 3 and m3.one_range(),
          "random workload: several areas, end to end (%d)" % len(m3.live()))
    check(not m3.unexpected, "random workload: no other SWIs")

    # Out of memory just as an area fills: no empty area left behind.
    m4 = fresh()
    m4.ram = 1024 * 1024
    got = fill(m4, 10, 200 * 1024)
    check(0 in got and all(a.size > 0 for a in m4.live()),
          "out of memory: no empty area left behind (%s)"
          % [(a.num, a.size) for a in m4.live()])
    m4.call(s["t_free"], got[0])
    check(m4.call(s["t_malloc"], 100 * 1024) != 0,
          "out of memory: freed space can be used again")

    # RISC OS refuses an area with a big maximum: asks for less.
    m5 = fresh()
    m5.refuse_big = True
    got = fill(m5, 8, 200 * 1024)
    check(all(got) and len(m5.live()) > 1 and m5.one_range(),
          "big maximum refused: carries on in smaller areas (%d failed)"
          % got.count(0))

    # Growing within an area doesn't read the area's details every time.
    m6 = fresh()
    for _ in range(400):
        m6.call(s["t_malloc"], 1000)
    reads = m6.counts.get((OS_DynamicArea, 2), 0)
    grows = m6.counts.get(OS_ChangeDynamicArea, 0)
    check(reads <= 2, "OS_DynamicArea 2 not on every growth (%d reads, %d "
          "grows)" % (reads, grows))

    # ---- The space after the first area is taken by another area ----
    m7 = fresh()
    m7.add_area(50, 0x10000000 + CAP, CAP, b"Someone else")
    blocks = fill(m7, 30, 64 * 1024)
    check(all(blocks), "space after taken: 30 x 64KB allocated (%d failed)"
          % blocks.count(0))
    seg = [a for a in m7.live() if a.name.startswith(b"Test Heap")
           and a.num != FIRST]
    check(seg and seg[0].base not in (0x10000000 + CAP,),
          "space after taken: went on in an area elsewhere")
    check(all(m7.covered(p, 64 * 1024) for p in blocks if p)
          and no_overlap((p, p + 64 * 1024) for p in blocks if p)
          and intact(m7, blocks, 64 * 1024),
          "space after taken: blocks inside areas, no overlaps, kept")
    big = m7.call(s["t_malloc"], 1536 * 1024)
    check(big and m7.covered(big, 1536 * 1024),
          "space after taken: a 1.5MB block across the new segment's areas")
    check(m7.areas[50].size == 0 and not m7.areas[50].deleted,
          "the other program's area is left alone")
    exit_check(m7, "space after taken")

    # No memory committed for nothing when the space after is taken: the
    # first area isn't filled up before the heap moves elsewhere.
    m9 = fresh()
    m9.add_area(50, 0x10000000 + CAP, CAP, b"Someone else")
    fill(m9, 9, 64 * 1024)
    first_before = m9.areas[FIRST].size
    m9.counts = {}
    big = m9.call(s["t_malloc"], 600 * 1024)
    check(big and m9.areas[FIRST].size <= first_before + 32 * 1024,
          "space after taken: the first area isn't grown for nothing "
          "(%d KB -> %d KB)" % (first_before // 1024,
                                m9.areas[FIRST].size // 1024))
    m9.counts = {}
    fill(m9, 20, 64 * 1024)
    made = m9.counts.get((OS_DynamicArea, 0), 0)
    check(made <= 6, "space after taken: few OS_DynamicArea 0 calls for 20 "
          "more blocks (%d)" % made)

    # Out of memory with RISC OS growing part of the way before the error:
    # the heap still only hands out memory that is there.
    m10 = fresh()
    m10.ram = 1300 * 1024
    m10.partial = True
    got = fill(m10, 12, 150 * 1024)
    check(0 in got and all(m10.covered(p, 150 * 1024) for p in got if p)
          and intact(m10, got, 150 * 1024),
          "partial growth: blocks only in committed memory, contents kept")
    m10.ram += 400 * 1024
    more = m10.call(s["t_malloc"], 200 * 1024)
    check(more and m10.covered(more, 200 * 1024),
          "partial growth: goes on when memory is free again")

    # ---- Fixed bases refused (RISC OS chooses every address) ----
    m8 = fresh(fixed=False)
    blocks = fill(m8, 40, 64 * 1024)
    check(all(blocks), "no fixed bases: 40 x 64KB allocated (%d failed)"
          % blocks.count(0))
    low = [a for a in m8.live() if a.base < 0x10000000]
    check(low and low[0].size > 0, "no fixed bases: an area below the first "
          "is used too")
    check(no_overlap((p, p + 64 * 1024) for p in blocks if p)
          and all(m8.owner(p, 64 * 1024) for p in blocks if p)
          and intact(m8, blocks, 64 * 1024),
          "no fixed bases: each block in one area, no overlaps, kept")
    for p in blocks[::2]:
        m8.call(s["t_free"], p)
    before = len(m8.live())
    again = fill(m8, 10, 60 * 1024)
    check(all(again) and len(m8.live()) == before,
          "no fixed bases: freed space reused without a new area")
    n_live = len(m8.live())
    huge = m8.call(s["t_malloc"], 1536 * 1024)
    check(huge == 0 and len(m8.live()) == n_live,
          "no fixed bases: a block bigger than an area is refused, nothing "
          "left behind")
    ok, live = random_workload(m8, 300, 100 * 1024)
    check(ok and no_overlap((p, p + n) for p, (n, _) in live.items()),
          "no fixed bases: random workload kept its contents")
    check(not m8.unexpected, "no fixed bases: no other SWIs")
    exit_check(m8, "no fixed bases")

    # ---- A small gap after the heap (2026-10-04 audit) ----
    # The first chained area halves its maximum until it fits a 1 MB gap.
    # That size used to become the cap for every later area, so the heap
    # stopped at about 8 + 63 x 1 MB.  Only maxima accepted at a base
    # RISC OS chooses are a limit now.
    saved_cap = globals()['CAP']
    globals()['CAP'] = 8 * MB
    m11 = fresh()
    m11.add_area(50, 0x10000000 + 8 * MB + 1 * MB, 8 * MB, b"Someone else")
    got = fill(m11, 200, 512 * 1024)
    heap = [a for a in m11.live() if a.name.startswith(b"Test Heap")]
    maxima = sorted(set(a.max for a in heap))
    check(all(got), "gap after the heap: 100 MB allocated in 512 KB blocks "
          "(%d failed; area maxima %s KB)"
          % (got.count(0), [x // 1024 for x in maxima]))
    small = [a for a in heap if a.max < 8 * MB]
    check(len(small) <= 1, "gap after the heap: only the gap's own area is "
          "small (%d areas under 8 MB)" % len(small))
    exit_check(m11, "gap after the heap")

    # Peer review of the first fix: with fixed bases refused and big
    # maxima refused (an older RISC OS), every new segment halved from
    # 128 MB again; a maximum accepted where RISC OS chose the base still
    # caps those requests.
    m12 = fresh(fixed=False)
    m12.refuse_big = True
    m12.counts = {}
    got = fill(m12, 40, 512 * 1024)
    made = m12.counts.get((OS_DynamicArea, 0), 0)
    check(all(got) and made <= 45, "older RISC OS: 40 x 512 KB with %d "
          "OS_DynamicArea 0 calls (at most 45)" % made)
    globals()['CAP'] = saved_cap

    print("heap_test: %d checks, %d failed" % (checks, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
