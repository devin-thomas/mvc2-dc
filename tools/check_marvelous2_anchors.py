#!/usr/bin/env python3
"""Check a 1ST_READ.BIN image against g-guthrie/marvelous2's retail-targeting disassembly.

Compares only items that sit directly on an address label (`loc_8cXXXXXX:`) in marvelous2's
build/bank*.asm: `#data` words, strings and pointers, plus `nop`/`rts`. Anchoring on labels
avoids tracking instruction sizes, so the counts don't depend on a full assembler. Read-only.

Usage:
    python tools/check_marvelous2_anchors.py build-inputs/1ST_READ_descrambled.bin path/to/marvelous2/build
"""
import collections
import glob
import re
import struct
import sys

BASE = 0x8C010000
LABEL = re.compile(r"^\s*[A-Za-z_]\w*?(8c[0-9a-f]{6})\s*:\s*(.*)$", re.I)
FIXED = {"nop": 0x0009, "rts": 0x000B}


def check(img, asm_dir):
    stats, bad = collections.Counter(), []
    for path in sorted(glob.glob(f"{asm_dir}/bank*.asm")):
        pending = None
        for raw in open(path, encoding="latin-1"):
            line = raw.split(";")[0].strip()
            if not line:
                continue
            m = LABEL.match(line)
            if m:
                pending, line = int(m.group(1), 16), m.group(2).strip()
                if not line:
                    continue
            if pending is None:
                continue
            addr, pending = pending, None
            off = addr - BASE
            if not 0 <= off < len(img) - 4:
                continue
            if line.startswith("#data ") or line == "#data":
                arg = line[5:].strip()
                hexv = re.fullmatch(r"0x([0-9a-fA-F]+)", arg)
                if hexv:
                    n = 2 if len(hexv.group(1)) <= 4 else 4
                    want, got = int(hexv.group(1), 16), struct.unpack_from("<H" if n == 2 else "<I", img, off)[0]
                    kind = f"data{n}"
                elif arg.startswith('"'):
                    want = arg[1 : arg.rindex('"')].encode("latin-1")
                    got, kind = img[off : off + len(want)], "string"
                else:
                    ptr = re.search(r"(8c[0-9a-f]{6})", arg, re.I)
                    if not ptr:
                        continue
                    want, got, kind = int(ptr.group(1), 16), struct.unpack_from("<I", img, off)[0], "pointer"
            else:
                op = line.split()[0].lower()
                if op not in FIXED:
                    continue
                want, got, kind = FIXED[op], struct.unpack_from("<H", img, off)[0], op
            ok = want == got
            stats[(kind, ok)] += 1
            if not ok:
                bad.append((addr, kind, want, got))
    return stats, bad


def main():
    img = open(sys.argv[1], "rb").read()
    stats, bad = check(img, sys.argv[2])
    ok = sum(v for (k, good), v in stats.items() if good)
    print(f"{sys.argv[1]}: {ok} anchored matches, {len(bad)} mismatches")
    for (kind, good), v in sorted(stats.items()):
        print(f"  {kind:8} {'ok' if good else 'bad':3} {v}")
    for addr, kind, want, got in bad[:50]:
        print(f"  {addr:08x} {kind} want={want!r} got={got!r}")


if __name__ == "__main__":
    main()
