#!/usr/bin/env python3
"""mkrozip.py OUT.zip DIR: zip DIR with RISC OS filetypes.

Names ending ,xxx (hex) lose the suffix and get the type in the Acorn
(ARC0, 0x4341) extra field, so SparkFS / !SparkPlug / InfoZip on RISC OS
restore the filetype. Other files are typed Text (fff)."""
import os, struct, sys, time, zipfile

def ro_extra(ftype, mtime):
    # RISC OS time: centiseconds since 1900-01-01
    cs = int((mtime + 2208988800) * 100)
    load = 0xFFF00000 | (ftype << 8) | ((cs >> 32) & 0xFF)
    exe = cs & 0xFFFFFFFF
    data = b"ARC0" + struct.pack("<IIII", load, exe, 0x33, 0)
    return struct.pack("<HH", 0x4341, len(data)) + data

def main(out, top):
    base = os.path.dirname(os.path.abspath(top))
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for root, dirs, files in os.walk(top):
            dirs.sort()
            rel = os.path.relpath(root, base)
            zi = zipfile.ZipInfo(rel + "/", time.localtime(os.path.getmtime(root))[:6])
            zi.extra = struct.pack("<HH", 0x4341, 20) + b"ARC0" + struct.pack("<IIII", 0, 0, 0x33, 0)
            z.writestr(zi, b"")
            for f in sorted(files):
                p = os.path.join(root, f)
                name, ftype = f, 0xFFF
                if len(f) > 4 and f[-4] == "," and all(c in "0123456789abcdefABCDEF" for c in f[-3:]):
                    name, ftype = f[:-4], int(f[-3:], 16)
                zi = zipfile.ZipInfo(os.path.join(rel, name), time.localtime(os.path.getmtime(p))[:6])
                zi.compress_type = zipfile.ZIP_DEFLATED
                zi.external_attr = 0o644 << 16
                zi.extra = ro_extra(ftype, os.path.getmtime(p))
                with open(p, "rb") as fh:
                    z.writestr(zi, fh.read())

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
