#!/usr/bin/env python3
"""Fds that aren't UnixLib's, with ARMEABISupport 1.05 and 1.08 (Unicorn).

Links tests/emu/fdop_stub.c with the built libunixlib.a and runs the real
close(), dup(), read(), write(), lseek(), lseek64(), fstat(), fsync(),
ftruncate(), isatty(), fcntl(), mmap(), munmap() and msync() on fds
outside UnixLib's table, with ARMEABISupport faked two ways:

  - 1.05: no FdOp or FileOp ("SWI not known"); shared memory fds are
    pointers, closed with ShmOp 2 and duplicated with ShmOp 3.  UnixLib
    must do exactly what it did before (EBADF for everything else).
  - 1.08: fds from 1000, FdOp for everything, ShmOp 2 is DUP (never to be
    used as close), FileOp for O_SHAREDFILE.  An fd it doesn't know gives
    its EBADF error.

  tests/emu/fdop_test.py [build dir]   (default build/work/build)

Needs the cross compiler (GCCSDK_ENV) and the Python module unicorn.
"""
import os
import struct
import subprocess
import sys
import tempfile

from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_INTR
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5,
                               UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR,
                               UC_ARM_REG_PC, UC_ARM_REG_CPSR, UC_ARM_REG_FP,
                               UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC)

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "build/work/build")
ENV = os.environ.get("GCCSDK_ENV", os.path.expanduser("~/gccsdk/env"))
TOOL = os.path.join(ENV, "bin/arm-riscos-gnueabihf-")

X = 0x20000
VFLAG = 1 << 28
OS_CallASWIR12 = 0x71
MMapOp, ShmOp, FdOp, FileOp = 0x59D04, 0x59D05, 0x59D06, 0x59D07

LOAD = 0x100000
RET = 0xFFF0
CSTACK = 0x800000
HEAP = 0x3000000          # buffers
MAPS = 0x4000000          # mmap results
ERRBLK = 0x3100000
PAGE = 4096

E_NOSWI = 0x1E6
E_EBADF = 0x81DC2C
E_EINVAL = 0x81DC28
EBADF, EINVAL, EOVERFLOW, ENODEV = 9, 22, 91, 19   # UnixLib's numbers
PTRFD = 0x1234560          # a 1.05 shared memory fd (a pointer)

fails = 0
checks = 0


def check(cond, what):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        print("FAIL:", what)


def link():
    out = os.path.join(tempfile.mkdtemp(), "fdop.elf")
    subprocess.run([TOOL + "gcc", "-O2", "-static", "-nostdlib", "-nostartfiles",
                    "-Wl,-Ttext=0x%x" % LOAD, "-Wl,-e,t_init",
                    "-D_GNU_SOURCE=1", "-D__UNIXLIB_CHUNKED_STACK=0",
                    "-I", os.path.join(REPO, "libunixlib/incl-local"),
                    "-isystem", os.path.join(REPO, "libunixlib/include"),
                    os.path.join(REPO, "tests/emu/fdop_stub.c"),
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


REGS = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
        UC_ARM_REG_R4, UC_ARM_REG_R5)


class Machine:
    def __init__(self, elf, syms, version, registered=True):
        self.version = version
        self.registered = registered
        self.syms = syms
        self.uc = uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        # VFP on (the library is built hard-float)
        uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))
        uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        top = max(v + m for v, _, m in segments(elf))
        uc.mem_map(LOAD, (top - LOAD + 0xFFFFF) & ~0xFFFFF)
        for vaddr, data, memsz in segments(elf):
            uc.mem_write(vaddr, data)
        uc.mem_map(0, 0x10000)
        uc.mem_map(CSTACK - 0x10000, 0x10000)
        uc.mem_map(HEAP, 0x10000)
        uc.mem_map(MAPS, 0x10000)
        uc.mem_map(ERRBLK, PAGE)
        uc.hook_add(UC_HOOK_INTR, self.swi)
        self.calls = []               # (swi, r0..r5) of ARMEABISupport calls
        # 1.08's fds: fd -> dict(type, data, pos, cloexec)
        self.fds = {1000: dict(type=0, data=bytearray(b"S" * 64), pos=0, cloexec=0),
                    1001: dict(type=1, data=bytearray(b"F" * 64), pos=0, cloexec=0)}
        self.maps = {}                # addr -> (len, fd)
        self.nextfd = 1002
        self.call(syms["t_init"])

    def reg(self, r, v=None):
        if v is None:
            return self.uc.reg_read(r)
        self.uc.reg_write(r, v & 0xFFFFFFFF)

    def error(self, num):
        self.uc.mem_write(ERRBLK, struct.pack("<I", num) + b"faked\0")
        self.reg(UC_ARM_REG_R0, ERRBLK)
        cpsr = self.reg(UC_ARM_REG_CPSR)
        self.reg(UC_ARM_REG_CPSR, cpsr | VFLAG)

    def swi(self, uc, intno, data):
        pc = self.reg(UC_ARM_REG_PC)
        num = struct.unpack("<I", uc.mem_read(pc - 4, 4))[0] & 0xFFFFFF
        num &= ~X
        if num == OS_CallASWIR12:
            num = self.reg(UC_ARM_REG_R12) & ~X
        cpsr = self.reg(UC_ARM_REG_CPSR)
        self.reg(UC_ARM_REG_CPSR, cpsr & ~VFLAG)
        r = [self.reg(x) for x in REGS]
        if num in (MMapOp, ShmOp, FdOp, FileOp):
            self.calls.append((num,) + tuple(r))
        if num == FdOp:
            return self.fdop(r) if self.version == 108 else self.error(E_NOSWI)
        if num == FileOp:
            if self.version != 108:
                return self.error(E_NOSWI)
            assert r[0] == 0
            self.fds[self.nextfd] = dict(type=1, data=bytearray(), pos=0, cloexec=0)
            self.reg(UC_ARM_REG_R0, self.nextfd)
            self.nextfd += 1
            return
        if num == ShmOp:
            if self.version == 105:
                if r[0] == 0:
                    self.reg(UC_ARM_REG_R0, PTRFD)
                elif r[0] in (2, 3):           # close, dup
                    if r[1] != PTRFD:
                        return self.error(E_EBADF)
                    if r[0] == 3:
                        self.reg(UC_ARM_REG_R0, PTRFD)
                else:
                    raise RuntimeError("1.05 ShmOp %d" % r[0])
            else:
                if r[0] == 0:
                    self.fds[self.nextfd] = dict(type=0, data=bytearray(64), pos=0, cloexec=0)
                    self.reg(UC_ARM_REG_R0, self.nextfd)
                    self.nextfd += 1
                elif r[0] == 2:                # DUP in 1.08 (not close!)
                    self.reg(UC_ARM_REG_R0, self.nextfd)
                    self.nextfd += 1
                else:
                    raise RuntimeError("1.08 ShmOp %d" % r[0])
            return
        if num == MMapOp:
            return self.mmapop(r)
        if num == 8 and r[0] in (5, 17, 20, 23):   # OS_File read info
            self.reg(UC_ARM_REG_R0, 0)         # not found
            return
        # Anything else (riscosify's look-ups): not there.
        self.error(0x1E4)

    def fdop(self, r):
        reason, fd = r[0], r[1]
        if fd >= 0x80000000:
            fd -= 1 << 32
        if not self.registered:
            return self.error(0x81DC23)        # process not known
        f = self.fds.get(fd)
        if f is None:
            return self.error(E_EBADF)
        if reason == 0:                        # VALIDATE
            self.reg(UC_ARM_REG_R1, f["type"])
        elif reason == 1:                      # CLOSE
            del self.fds[fd]
        elif reason == 2:                      # SEEK
            base = {0: 0, 1: f["pos"], 2: len(f["data"])}[r[3]]
            f["pos"] = base + (r[2] - (1 << 32) if r[2] >= 0x80000000 else r[2])
            self.reg(UC_ARM_REG_R0, f["pos"])
        elif reason == 3:                      # READ
            n = min(r[3], len(f["data"]) - f["pos"])
            self.uc.mem_write(r[2], bytes(f["data"][f["pos"]:f["pos"] + n]))
            f["pos"] += n
            self.reg(UC_ARM_REG_R0, n)
        elif reason == 4:                      # WRITE
            self.reg(UC_ARM_REG_R0, r[3])
        elif reason == 5:                      # GETINFO: file, size 64
            for x, v in zip(REGS, (1, ERRBLK + 16, 0xFFFFFF00, 0, 64, 3)):
                self.reg(x, v)
            self.uc.mem_write(ERRBLK + 16, b"shared\0")
        elif reason == 6:                      # SETLOCK
            pass
        elif reason == 7:                      # GETLOCK: say it's free
            self.reg(UC_ARM_REG_R2, 3)         # F_UNLCK
            self.reg(UC_ARM_REG_R3, 1)
            self.reg(UC_ARM_REG_R4, 0x77777777)
            self.reg(UC_ARM_REG_R5, 0x66666666)
        elif reason in (8, 9):                 # FSYNC, FTRUNCATE
            pass
        elif reason == 11:                     # DUP2
            self.fds[r[2]] = dict(f)
        elif reason == 10:                     # DUP
            self.fds[self.nextfd] = dict(f)
            self.reg(UC_ARM_REG_R0, self.nextfd)
            self.nextfd += 1
        elif reason == 12:                     # GETFD
            self.reg(UC_ARM_REG_R0, f["cloexec"])
        elif reason == 13:                     # SETFD
            f["cloexec"] = r[2]
        else:
            raise RuntimeError("FdOp %d" % reason)

    def mmapop(self, r):
        if r[0] == 0:                          # MAP
            addr = MAPS + PAGE * len(self.maps)
            fd = r[5] - (1 << 32) if r[5] >= 0x80000000 else r[5]
            self.maps[addr] = (r[2], fd)
            self.reg(UC_ARM_REG_R0, addr)
        elif r[0] == 1:                        # UNMAP
            self.maps.pop(r[1], None)
        elif r[0] == 4:                        # GET_INFO
            if r[1] not in self.maps:
                return self.error(E_EINVAL)
            ln, fd = self.maps[r[1]]
            for x, v in zip(REGS, (r[1], ln, 3, 1, fd, 0)):
                self.reg(x, v)
        elif r[0] == 7:                        # MSYNC (1.07 and later)
            if self.version != 108:
                raise RuntimeError("MSYNC on 1.05")
        else:
            raise RuntimeError("MMapOp %d" % r[0])

    def call(self, name, *args):
        uc = self.uc
        for r, v in zip(REGS, args):
            self.reg(r, v)
        self.reg(UC_ARM_REG_SP, CSTACK - 0x100)
        self.reg(UC_ARM_REG_FP, CSTACK - 0x100)
        self.reg(UC_ARM_REG_LR, RET)
        addr = name if isinstance(name, int) else self.syms[name]
        uc.emu_start(addr, RET, count=5000000)
        v = self.reg(UC_ARM_REG_R0)
        return v - (1 << 32) if v >= 0x80000000 else v

    def errno(self):
        return self.call("t_errno")

    def ops(self, swi):
        return [c[1] for c in self.calls if c[0] == swi]


def fails_with(m, result, err, what):
    e = m.errno()
    check(result == -1 and e == err,
          "%s: want -1, errno %d; got %d, errno %d" % (what, err, result, e))


def test(elf, syms, v):
    tag = "1.%02d" % (v - 100)
    M = lambda: Machine(elf, syms, v)

    m = M()
    check(m.call("t_have_fdop") == (1 if v == 108 else 0), tag + ": FdOp detected")

    # A fd nobody knows: EBADF everywhere, as before (not "SWI not known").
    m = M()
    for name, args in (("t_close", (5,)), ("t_read", (5, HEAP, 4)),
                       ("t_write", (5, HEAP, 4)), ("t_lseek", (5, 0, 0)),
                       ("t_fsync", (5,)), ("t_fstat_size", (5,)),
                       ("t_ftruncate", (5, 0)), ("t_getfd", (5,)),
                       ("t_dup", (5,)), ("t_close", (2000,))):
        m.calls = []
        fails_with(m, m.call(name, *args), EBADF, "%s: %s%s on an unknown fd" % (tag, name, args))
    check(not m.ops(ShmOp), tag + ": no ShmOp for unknown fds")

    # isatty: 0 for a bad fd (was -1), with EBADF.
    m = M()
    r = m.call("t_isatty", 5)
    check(r == 0 and m.errno() == EBADF, "%s: isatty(bad) = %d errno %d" % (tag, r, m.errno()))

    # UnixLib's own fds never reach ARMEABISupport.
    m = M()
    m.calls = []
    check(m.call("t_unixlib_fd") == 1 and not [c for c in m.calls if c[1:3] != (0, 0xFFFFFFFF)],
          "%s: a UnixLib fd stays UnixLib's (%s)" % (tag, m.calls))
    # MAP_ANON ignores fd.
    m = M()
    check(m.call("t_mmap_anon", 0xFFFFFFFE) != 0 and m.call("t_mmap_anon", 3) != 0,
          tag + ": MAP_ANON with any fd")
    m = M()
    fails_with(m, m.call("t_close", -1), EBADF if v == 108 else EBADF, tag + ": close(-1)")

    if v == 105:
        # Shared memory as before: ShmOp 2 closes, 3 dups.
        m = M()
        fd = m.call("t_shm_open")
        check(fd == PTRFD, "1.05: shm_open gives the pointer fd")
        check(m.call("t_dup", fd) == PTRFD and m.ops(ShmOp)[-1] == 3, "1.05: dup = ShmOp 3")
        check(m.call("t_ftruncate", fd, 4096) == 0, "1.05: ftruncate is a no-op")
        check(m.call("t_getfd", fd) == 0, "1.05: F_GETFD 0 as before")
        g = m.call("t_getlk", fd) & 0xFFFFFFFF
        check(struct.unpack("<I", m.uc.mem_read(g, 4))[0] == 0xAAAAAAAA, "1.05: F_GETLK 0 as before")
        p = m.call("t_mmap", fd, 4096) & 0xFFFFFFFF
        check(p != 0, "1.05: mmap of shm works")
        fails_with(m, m.call("t_msync", p, 4096), EINVAL, "1.05: msync of shm")
        check(m.call("t_close", fd) == 0 and m.ops(ShmOp)[-1] == 2, "1.05: close = ShmOp 2")
        fails_with(m, m.call("t_read", fd, HEAP, 4), EBADF, "1.05: read of shm (as before)")
        check(m.call("t_open_shared") == -2, "1.05: O_SHAREDFILE falls back to an ordinary open")
        return

    # 1.08.
    m = M()
    fd = m.call("t_shm_open")
    check(fd == 1002, "1.08: shm_open gives fd 1002")
    m.calls = []
    d = m.call("t_dup", fd)
    check(d == 1003 and [o for o in m.ops(FdOp) if o != 0] == [10] and not m.ops(ShmOp),
          "1.08: dup = FdOp DUP, never ShmOp 2 (got %d, %s)" % (d, m.calls))
    check(m.call("t_ftruncate", fd, 4096) == 0 and m.calls[-1][1:4] == (9, fd, 4096),
          "1.08: ftruncate = FdOp FTRUNCATE")
    check(m.call("t_fsync", fd) == 0 and m.calls[-1][1] == 8, "1.08: fsync = FdOp FSYNC")
    check(m.call("t_setfd", fd, 1) == 0 and m.call("t_getfd", fd) == 1,
          "1.08: F_SETFD / F_GETFD close-on-exec")
    check(m.call("t_getfl", fd) == 0, "1.08: F_GETFL 0")
    g = m.call("t_getlk", fd) & 0xFFFFFFFF
    before, ltype, lwhence, lstart, llen, lpid, after = struct.unpack(
        "<IhhiiiI", bytes(m.uc.mem_read(g, 24)))
    check(before == 0xAAAAAAAA and after == 0xBBBBBBBB, "1.08: F_GETLK result 0, guards intact")
    check((ltype, lwhence, lstart, llen, lpid) == (3, 1, 0x77777777, 0x66666666, 0x5555),
          "1.08: F_GETLK fills type/whence/start/len only (%s)"
          % ((ltype, lwhence, lstart, llen, lpid),))
    check(m.call("t_setlk", fd) == 0 and m.calls[-1][1:7] == (6, fd, 1, 1, 100, 200),
          "1.08: F_SETLK passes the lock (%s)" % (m.calls[-1],))
    check(m.call("t_read", 1001, HEAP, 8) == 8 and bytes(m.uc.mem_read(HEAP, 8)) == b"F" * 8,
          "1.08: read = FdOp READ")
    check(m.call("t_write", 1001, HEAP, 8) == 8, "1.08: write = FdOp WRITE")
    check(m.call("t_lseek", 1001, 5, 0) == 5, "1.08: lseek = FdOp SEEK")
    fails_with(m, m.call("t_lseek64_big", 1001), EOVERFLOW, "1.08: lseek64 past 4GB")
    check(m.call("t_fstat_size", 1001) == 64, "1.08: fstat = FdOp GETINFO (size)")
    check(m.call("t_isatty", 1001) == 0, "1.08: isatty 0")

    # mmap: shared memory isn't read in; a shared file is, through FdOp READ.
    m = M()
    m.calls = []
    p = m.call("t_mmap", 1000, 64) & 0xFFFFFFFF
    check(p != 0 and 3 not in m.ops(FdOp), "1.08: mmap of shm, no reads")
    m.calls = []
    q = m.call("t_mmap", 1001, 64) & 0xFFFFFFFF
    check(q != 0 and bytes(m.uc.mem_read(q, 4)) == b"FFFF", "1.08: mmap of a shared file reads it in")
    m.calls = []
    check(m.call("t_msync", q, 64) == 0 and m.ops(MMapOp)[-1] == 7, "1.08: msync = MMapOp MSYNC")
    m.calls = []
    check(m.call("t_munmap", q, 64) == 0 and 4 not in m.ops(FdOp) and m.ops(MMapOp)[-1] == 1,
          "1.08: munmap leaves the write-back to ARMEABISupport")
    r = m.call("t_mmap", 7, 64)
    check(r == 0 and m.errno() == EBADF and not m.maps.get(MAPS + 2 * PAGE),
          "1.08: mmap of a bad fd: EBADF, mapping undone")

    # Positions survive an mmap of a shared file.
    m = M()
    m.call("t_lseek", 1001, 7, 0)
    m.call("t_mmap", 1001, 64)
    check(m.call("t_lseek", 1001, 0, 1) == 7, "1.08: mmap restores the file position")
    # dup2 to one of ARMEABISupport's numbers, and not to UnixLib's.
    check(m.call("t_dup2", 1001, 1500) == 1500 and 1500 in m.fds, "1.08: dup2 = FdOp DUP2")
    fails_with(m, m.call("t_dup2", 1001, 5), EINVAL, "1.08: dup2 to a UnixLib number")
    check(m.call("t_setlkw", 1001) == 0 and m.calls[-1][1] == 6, "1.08: F_SETLKW = SETLOCK")
    m.fds[2000] = dict(type=1, data=bytearray(), pos=0, cloexec=0)
    check(m.call("t_dupfd", 1001, 2000) == 2001 and 2001 in m.fds,
          "1.08: F_DUPFD from 2000: the lowest free number from there")
    # msync of shared memory: nothing to do.
    p = m.call("t_mmap", 1000, 64) & 0xFFFFFFFF
    m.calls = []
    check(m.call("t_msync", p, 64) == 0 and 7 not in m.ops(MMapOp), "1.08: msync of shm: 0")
    # A program ARMEABISupport doesn't know: EBADF, and FdOp is still seen.
    m = Machine(elf, syms, 108, registered=False)
    check(m.call("t_have_fdop") == 1, "1.08 unregistered: FdOp detected")
    fails_with(m, m.call("t_close", 1000), EBADF, "1.08 unregistered: close")

    m = M()
    m.calls = []
    fd = m.call("t_open_shared")
    check(fd >= 1002 and m.ops(FileOp) and m.calls[-1][3] & 0x40000 == 0,
          "1.08: O_SHAREDFILE = FileOp OPEN, flag cleared (%s)" % (m.calls,))
    check(m.call("t_close", 1000) == 0 and m.calls[-1][1:3] == (1, 1000) and 1000 not in m.fds,
          "1.08: close = FdOp CLOSE")


def main():
    try:
        elf = link()
    except (OSError, subprocess.CalledProcessError) as e:
        print("can't link the test image:", e)
        return 1
    syms = symbols(elf)
    for v in (105, 108):
        test(elf, syms, v)
    print("fdop_test: %d checks, %d failed" % (checks, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except UcError as e:
        print("FAIL: emulator error:", e)
        sys.exit(1)
