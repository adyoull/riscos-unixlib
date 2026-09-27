#!/usr/bin/env python3
"""Run the thread ticker routines in an ARM emulator (Unicorn).

Checks the real machine code from a build, with the RISC OS SWIs faked:
  - SharedUnixLibrary 1.17's SharedUnixLibrary_Ticker SWI, its copy of the
    ticker routines (internal/ticker.s), the zeroing of the new process
    field and the clean-up in sul_exit;
  - UnixLib's copy of the same routines (pthread/_context.s), run from a
    different address, as __pthread_prog_init does with an older SUL.

  tests/emu/ticker_test.py [build dir]   (default build/work/build)

Needs the cross toolchain's nm/objcopy (GCCSDK_ENV) and the Python module
unicorn (pip install unicorn). tests/check.sh skips it without them.
"""
import os
import subprocess
import sys
import tempfile

from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_INTR
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5,
                               UC_ARM_REG_R11, UC_ARM_REG_R12, UC_ARM_REG_SP,
                               UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR)

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "build/work/build")
ENV = os.environ.get("GCCSDK_ENV", os.path.expanduser("~/gccsdk/env"))
TOOL = os.path.join(ENV, "bin/arm-riscos-gnueabihf-")

# Offsets in the pthread RMA block (asm_dec.s) and the SUL process struct
STARTED, WORKSEM, CBSEM = 88, 92, 96
UPCALL_ADDR, UPCALL_R12 = 76, 80
TICKS, FOREIGN, F_HANDLER, F_R12, PRE, POST = 120, 124, 128, 132, 136, 140
TICKER_CODE, BLOCK_SIZE = 152, 472
PROC_NEXT, PROC_STATUS, PROC_PPID, PROC_PRIVATEWORD = 0, 96, 24, 128 + 56
PROC_TICKERBLOCK, PROC_SIZE = 276, 280

VFLAG, CFLAG, ZFLAG = 1 << 28, 1 << 29, 1 << 30
X = 0x20000
OS_CallEvery, OS_RemoveTickerEvent, OS_SetCallBack = 0x3C, 0x3D, 0x1B
OS_ChangeEnvironment, OS_Module, OS_Exit = 0x40, 0x1E, 0x11

RET = 0xFFF0          # "return address": emulation stops there
MODBASE = 0x10000     # where the SUL module is loaded
RAM = 0x40000         # fake RMA
STACK = 0x80000

fails = 0
checks = 0


def check(cond, what):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        print("FAIL:", what)


def symbols(obj):
    out = subprocess.run([TOOL + "nm", obj], check=True, capture_output=True,
                         text=True).stdout
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in "tT":
            syms[parts[2]] = int(parts[0], 16)
    return syms


def text(obj):
    with tempfile.NamedTemporaryFile() as f:
        subprocess.run([TOOL + "objcopy", "-O", "binary", "-j", ".text", obj,
                        f.name], check=True)
        return open(f.name, "rb").read()


class Machine:
    """ARM with fake SWIs. upcall = what OS_ChangeEnvironment 16 returns."""

    def __init__(self):
        self.mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        self.mu.mem_map(0, 0x100000)
        self.mu.mem_write(RET, b"\xfe\xff\xff\xea")      # B .
        self.swis = []
        self.upcall = (0, 0)
        self.alloc = RAM + 0x8000
        self.mu.hook_add(UC_HOOK_INTR, self.swi)

    def swi(self, mu, intno, _):
        pc = mu.reg_read(UC_ARM_REG_PC)
        n = int.from_bytes(mu.mem_read(pc - 4, 4), "little") & 0xFFFFFF
        num = n & ~X
        r = [mu.reg_read(x) for x in (UC_ARM_REG_R0, UC_ARM_REG_R1,
                                      UC_ARM_REG_R2, UC_ARM_REG_R3)]
        self.swis.append((num, r))
        cpsr = mu.reg_read(UC_ARM_REG_CPSR) & ~VFLAG
        if num == OS_ChangeEnvironment and r[0] == 16:
            mu.reg_write(UC_ARM_REG_R1, self.upcall[0])
            mu.reg_write(UC_ARM_REG_R2, self.upcall[1])
            mu.reg_write(UC_ARM_REG_R3, 0)
        elif num == OS_Module and r[0] == 6:
            # Claim: garbage-filled, so fields left unset show up
            mu.mem_write(self.alloc, b"\xaa" * r[3])
            mu.reg_write(UC_ARM_REG_R2, self.alloc)
            self.alloc += (r[3] + 15) & ~15
        elif num == OS_Exit:
            mu.emu_stop()
        mu.reg_write(UC_ARM_REG_CPSR, cpsr)

    def w(self, addr, val):
        self.mu.mem_write(addr, (val & 0xFFFFFFFF).to_bytes(4, "little"))

    def r(self, addr):
        return int.from_bytes(self.mu.mem_read(addr, 4), "little")

    def call(self, addr, regs=None, flags=0):
        """Call addr with regs {reg: value}, lr = RET. Returns r0-r5, cpsr."""
        self.swis = []
        mu = self.mu
        for reg in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                    UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5):
            mu.reg_write(reg, 0x11110000 + reg)
        for reg, val in (regs or {}).items():
            mu.reg_write(reg, val)
        mu.reg_write(UC_ARM_REG_SP, STACK)
        mu.reg_write(UC_ARM_REG_LR, RET)
        cpsr = (mu.reg_read(UC_ARM_REG_CPSR) & 0x0FFFFFFF) | flags
        mu.reg_write(UC_ARM_REG_CPSR, cpsr)
        mu.emu_start(addr, RET, count=5000)
        out = [mu.reg_read(x) for x in (UC_ARM_REG_R0, UC_ARM_REG_R1,
                                        UC_ARM_REG_R2, UC_ARM_REG_R3,
                                        UC_ARM_REG_R4, UC_ARM_REG_R5)]
        check(mu.reg_read(UC_ARM_REG_SP) == STACK or mu.reg_read(UC_ARM_REG_PC) != RET,
              "stack balanced after call to %#x" % addr)
        return out, mu.reg_read(UC_ARM_REG_CPSR)

    def called(self, num):
        return [r for n, r in self.swis if n == num]


def new_block(m, addr, key):
    m.mu.mem_write(addr, b"\0" * BLOCK_SIZE)
    m.w(addr + UPCALL_ADDR, 0xC0DE00)      # SUL's upcall handler (fake)
    m.w(addr + UPCALL_R12, key)
    return addr


def test_routines(m, name, handler, start, stop, pre, post, block):
    """The five routines, wherever they are, with ip = block."""
    ip = {UC_ARM_REG_R12: block}

    # start
    m.call(start, ip)
    ce = m.called(OS_CallEvery)
    check(ce and ce[0][:3] == [1, handler, block],
          name + ": start: OS_CallEvery 1, handler, block")
    check(m.r(block + STARTED) == 1, name + ": start sets ticker_started")
    m.call(start, ip)
    check(not m.called(OS_CallEvery), name + ": start again does nothing")

    # handler, our task paged in
    m.upcall = (0xC0DE00, m.r(block + UPCALL_R12))
    out, _ = m.call(handler, ip)
    check(m.called(OS_SetCallBack), name + ": handler sets a callback")
    check(m.r(block + TICKS) == 1, name + ": handler counts ticks")
    check(out[:4] == [0x11110000 + x for x in (UC_ARM_REG_R0, UC_ARM_REG_R1,
                                               UC_ARM_REG_R2, UC_ARM_REG_R3)],
          name + ": handler preserves r0-r3")
    m.w(block + WORKSEM, 1)
    m.call(handler, ip)
    check(not m.called(OS_SetCallBack), name + ": no callback in a critical section")
    m.w(block + WORKSEM, 0)
    m.w(block + CBSEM, 1)
    m.call(handler, ip)
    check(not m.called(OS_SetCallBack), name + ": no callback during a switch")
    m.w(block + CBSEM, 0)

    # handler, another task paged in (same SUL handler, another key)
    m.upcall = (0xC0DE00, 0x5A5A5A)
    m.call(handler, ip)
    check(not m.called(OS_SetCallBack), name + ": foreign tick: no callback")
    check(m.r(block + FOREIGN) == 1, name + ": foreign tick counted")
    check((m.r(block + F_HANDLER), m.r(block + F_R12)) == (0xC0DE00, 0x5A5A5A),
          name + ": foreign handler/R12 noted")
    m.upcall = (0x3800000, 0)                   # a non-UnixLib task
    m.call(handler, ip)
    check(m.r(block + FOREIGN) == 2 and m.r(block + TICKS) == 5,
          name + ": counts: 5 ticks, 2 foreign")

    # pre-filter: R0 (event mask) and flags preserved, ticker stopped
    out, cpsr = m.call(pre, {UC_ARM_REG_R12: block, UC_ARM_REG_R0: 0x1234},
                       CFLAG | ZFLAG)
    rt = m.called(OS_RemoveTickerEvent)
    check(rt and rt[0][:2] == [handler, block],
          name + ": pre-filter: OS_RemoveTickerEvent handler, block")
    check(m.r(block + STARTED) == 0 and m.r(block + PRE) == 1,
          name + ": pre-filter stops and counts")
    check(out[0] == 0x1234 and cpsr & (CFLAG | ZFLAG | VFLAG) == CFLAG | ZFLAG,
          name + ": pre-filter preserves R0 and flags")
    m.call(stop, ip)
    check(not m.called(OS_RemoveTickerEvent), name + ": stop when stopped does nothing")

    # post-filter
    out, cpsr = m.call(post, {UC_ARM_REG_R12: block, UC_ARM_REG_R0: 7}, ZFLAG)
    check(m.called(OS_CallEvery) and m.r(block + STARTED) == 1
          and m.r(block + POST) == 1, name + ": post-filter starts and counts")
    check(out[0] == 7 and cpsr & (CFLAG | ZFLAG | VFLAG) == ZFLAG,
          name + ": post-filter preserves R0 and flags")


def swi(m, sul, number, regs):
    """Call SUL's SWI handler as the kernel would: r11 = SWI offset,
    r12 = private word pointer."""
    regs = dict(regs)
    regs[UC_ARM_REG_R11] = number
    regs[UC_ARM_REG_R12] = PRIVWORD
    return m.call(MODBASE + sul["swi_handler"], regs)


PRIVWORD = RAM          # module private word -> list head word
HEAD = RAM + 0x10


def test_sul(build):
    sul = symbols(os.path.join(build, "sul.o"))
    code = open(os.path.join(build, "sul"), "rb").read()
    m = Machine()
    m.mu.mem_write(MODBASE, code)
    help_ = code[code.index(b"SharedUnixLibrary\t"):][:40]
    check(b"1.17" in help_, "SUL help string says 1.17: %r" % help_)

    proc = RAM + 0x100
    m.mu.mem_write(proc, b"\0" * PROC_SIZE)
    m.w(PRIVWORD, HEAD)
    m.w(HEAD, proc)
    block = new_block(m, RAM + 0x1000, proc)
    a = lambda s: MODBASE + sul[s]

    out, cpsr = swi(m, sul, 5, {UC_ARM_REG_R0: 3, UC_ARM_REG_R1: proc})
    check(not cpsr & VFLAG, "Ticker 3: no error")
    check(out[1:3] == [a("sul_ticker_prefilter"), a("sul_ticker_postfilter")],
          "Ticker 3 returns the filter routines")
    out, cpsr = swi(m, sul, 5, {UC_ARM_REG_R0: 3, UC_ARM_REG_R1: proc + 4})
    check(cpsr & VFLAG and m.r(out[0]) == 0x81A401, "Ticker: unknown key -> error")
    out, cpsr = swi(m, sul, 5, {UC_ARM_REG_R0: 4, UC_ARM_REG_R1: proc})
    check(cpsr & VFLAG and m.r(out[0]) == 0x81A400, "Ticker: bad reason -> error")
    out, cpsr = swi(m, sul, 6, {})
    check(cpsr & VFLAG, "SWI 6 is unknown")

    out, cpsr = swi(m, sul, 5, {UC_ARM_REG_R0: 0, UC_ARM_REG_R1: proc,
                                UC_ARM_REG_R2: block})
    check(not cpsr & VFLAG and out[:3] == [0, proc, block],
          "Ticker 0: no error, R0-R2 preserved")
    check(m.r(proc + PROC_TICKERBLOCK) == block and m.r(block + STARTED) == 1
          and m.called(OS_CallEvery)[0][:3] == [1, a("sul_ticker_handler"), block],
          "Ticker 0 starts SUL's handler on the block")
    swi(m, sul, 5, {UC_ARM_REG_R0: 1, UC_ARM_REG_R1: proc})
    check(m.r(block + STARTED) == 0 and m.r(proc + PROC_TICKERBLOCK) == block,
          "Ticker 1 stops, keeps the block")
    swi(m, sul, 5, {UC_ARM_REG_R0: 0, UC_ARM_REG_R1: proc, UC_ARM_REG_R2: block})
    block2 = new_block(m, RAM + 0x2000, proc)
    swi(m, sul, 5, {UC_ARM_REG_R0: 0, UC_ARM_REG_R1: proc, UC_ARM_REG_R2: block2})
    check(m.called(OS_RemoveTickerEvent)[0][:2] == [a("sul_ticker_handler"), block]
          and m.r(block2 + STARTED) == 1, "Ticker 0 with a new block stops the old one")
    swi(m, sul, 5, {UC_ARM_REG_R0: 2, UC_ARM_REG_R1: proc})
    check(m.r(block2 + STARTED) == 0 and m.r(proc + PROC_TICKERBLOCK) == 0,
          "Ticker 2 stops and forgets the block")

    test_routines(m, "SUL", a("sul_ticker_handler"), a("sul_ticker_start"),
                  a("sul_ticker_stop"), a("sul_ticker_prefilter"),
                  a("sul_ticker_postfilter"), new_block(m, RAM + 0x3000, proc))

    # sul_exit stops a ticker the program left running
    m.w(proc + PROC_PPID, 1)
    m.w(proc + PROC_PRIVATEWORD, PRIVWORD)
    block3 = new_block(m, RAM + 0x4000, proc)
    swi(m, sul, 5, {UC_ARM_REG_R0: 0, UC_ARM_REG_R1: proc, UC_ARM_REG_R2: block3})
    m.call(a("sul_exit"), {UC_ARM_REG_R0: proc >> 2, UC_ARM_REG_R1: 0})
    check(m.called(OS_Exit), "sul_exit reaches OS_Exit")
    rt = m.called(OS_RemoveTickerEvent)
    check(rt and rt[0][:2] == [a("sul_ticker_handler"), block3]
          and m.r(block3 + STARTED) == 0 and m.r(proc + PROC_TICKERBLOCK) == 0,
          "sul_exit removes the ticker")

    # SharedUnixLibrary_Initialise: a new process has no ticker block
    m.w(HEAD, 0)
    out, cpsr = swi(m, sul, 4, {UC_ARM_REG_R0: 117})
    check(not cpsr & VFLAG, "Initialise: no error")
    check(m.r(out[0] + PROC_TICKERBLOCK) == 0, "Initialise zeroes the ticker block")


def test_copy(build):
    obj = os.path.join(build, "_context.o")
    syms = symbols(obj)
    code = text(obj)
    lo, hi = syms["__pthread_call_every_code"], syms["__pthread_call_every_code_end"]
    check(hi - lo <= 320, "UnixLib's routines fit in 320 bytes (%d)" % (hi - lo))
    offs = [int.from_bytes(code[hi + 4 * i:hi + 4 * i + 4], "little") for i in range(4)]
    m = Machine()
    block = new_block(m, RAM + 0x5004, 0x1234560)   # odd place on purpose
    m.mu.mem_write(block + TICKER_CODE, code[lo:hi])
    base = block + TICKER_CODE
    test_routines(m, "copy", base, base + offs[0], base + offs[1],
                  base + offs[2], base + offs[3], block)


def main():
    for f in ("sul", "sul.o", "_context.o"):
        if not os.path.exists(os.path.join(BUILD, f)):
            print("ticker_test: no %s in %s (build UnixLib first)" % (f, BUILD))
            return 2
    try:
        test_sul(BUILD)
        test_copy(BUILD)
    except UcError as e:
        print("FAIL: emulator:", e)
        return 1
    print("ticker_test: %d checks, %d failed" % (checks, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
