#!/usr/bin/env python3
"""Run the thread ticker routines in an ARM emulator (Unicorn).

Checks the real machine code from a build, with the RISC OS SWIs faked:
  - the PThreadTicker module (module/pthticker.s): its header, interface
    table, workspace, attach/detach and refusing to die while in use, and
    its copy of the ticker routines (internal/ticker.s);
  - UnixLib's copy of the same routines (pthread/_context.s), run from a
    different address, as UnixLib does when the module isn't loaded.

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
                               UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12, UC_ARM_REG_SP,
                               UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR)

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "build/work/build")
ENV = os.environ.get("GCCSDK_ENV", os.path.expanduser("~/gccsdk/env"))
TOOL = os.path.join(ENV, "bin/arm-riscos-gnueabihf-")

# Offsets in the pthread RMA block (asm_dec.s)
STARTED, WORKSEM, CBSEM = 88, 92, 96
UPCALL_ADDR, UPCALL_R12 = 76, 80
TICKS, FOREIGN, F_HANDLER, F_R12, PRE, POST = 120, 124, 128, 132, 136, 140
TICKER_CODE, BLOCK_SIZE = 152, 472

VFLAG, CFLAG, ZFLAG = 1 << 28, 1 << 29, 1 << 30
X = 0x20000
OS_CallEvery, OS_RemoveTickerEvent, OS_SetCallBack = 0x3C, 0x3D, 0x1B
OS_ChangeEnvironment, OS_Module, OS_Exit = 0x40, 0x1E, 0x11

RET = 0xFFF0          # "return address": emulation stops there
MODBASE = 0x10000     # where the module is loaded
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


PRIVWORD = RAM          # the module's private word


def test_module(build):
    syms = symbols(os.path.join(build, "pthticker.o"))
    code = open(os.path.join(build, "pthticker"), "rb").read()
    m = Machine()
    m.mu.mem_write(MODBASE, code)
    a = lambda s: MODBASE + syms[s]
    word = lambda off: int.from_bytes(code[off:off + 4], "little")

    # Header: no SWIs, 32-bit flag; the interface table at &34
    check(word(4) == syms["init_code"] and word(8) == syms["final_code"],
          "header: init and final entries")
    check(all(word(o) == 0 for o in (0x1C, 0x20, 0x24, 0x28)), "header: no SWIs")
    check(word(word(0x30)) & 1, "header: 32-bit compatible")
    title = code[word(0x10):code.index(b"\0", word(0x10))]
    check(title == b"PThreadTicker", "title %r" % title)
    check(word(0x34) == 0x6B545450 and word(0x38) == 1 and word(0x3C) == 7,
          "interface table: PTTk, version 1, 7 entries")
    names = ["pt_handler", "pt_start", "pt_stop", "pt_prefilter",
             "pt_postfilter", "pt_attach", "pt_detach"]
    offs = [word(0x40 + 4 * i) for i in range(7)]
    check(offs == [syms[n] for n in names], "interface table offsets")

    # Initialisation claims a zeroed workspace word
    m.w(PRIVWORD, 0)
    _, cpsr = m.call(a("init_code"), {UC_ARM_REG_R12: PRIVWORD})
    ws = m.r(PRIVWORD)
    check(not cpsr & VFLAG and ws and m.r(ws) == 0, "init: workspace, count 0")

    # attach/detach count; finalisation refuses while in use
    for _ in range(2):
        out, cpsr = m.call(MODBASE + offs[5], {UC_ARM_REG_R12: ws,
                                               UC_ARM_REG_R0: 99}, ZFLAG)
    check(m.r(ws) == 2 and out[0] == 99 and cpsr & ZFLAG, "attach counts, preserves")
    out, cpsr = m.call(a("final_code"), {UC_ARM_REG_R12: PRIVWORD, UC_ARM_REG_R10: 0})
    check(cpsr & VFLAG and b"in use" in bytes(m.mu.mem_read(out[0] + 4, 60)),
          "final refuses while attached")
    check(m.r(PRIVWORD) == ws, "workspace kept")
    for _ in range(3):
        m.call(MODBASE + offs[6], {UC_ARM_REG_R12: ws})
    check(m.r(ws) == 0, "detach counts down, not below 0")
    out, cpsr = m.call(a("final_code"), {UC_ARM_REG_R12: PRIVWORD, UC_ARM_REG_R10: 0})
    check(not cpsr & VFLAG and m.r(PRIVWORD) == 0
          and m.called(OS_Module) and m.called(OS_Module)[0][:1] == [7],
          "final frees the workspace when nobody is attached")

    test_routines(m, "module", *[MODBASE + o for o in offs[:5]],
                  new_block(m, RAM + 0x3000, 0x4000))


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
    for f in ("pthticker", "pthticker.o", "_context.o"):
        if not os.path.exists(os.path.join(BUILD, f)):
            print("ticker_test: no %s in %s (build UnixLib first)" % (f, BUILD))
            return 2
    try:
        test_module(BUILD)
        test_copy(BUILD)
    except UcError as e:
        print("FAIL: emulator:", e)
        return 1
    print("ticker_test: %d checks, %d failed" % (checks, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
