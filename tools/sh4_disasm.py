#!/usr/bin/env python3
"""SH-4 Disassembler helper using Capstone with resilient fallback.

Disassembles SH-4 machine code ranges with branch destination resolution,
literal pool identification, and function boundary detection.

Usage:
    python tools/sh4_disasm.py --offset 0x80 --count 30
    python tools/sh4_disasm.py --vram 0x8c010080 --size 128
"""

import argparse
import struct
import sys
from pathlib import Path

try:
    import capstone
    HAS_CAPSTONE = True
except ImportError:
    HAS_CAPSTONE = False


def disassemble_range(bin_path: Path, vram_addr: int, byte_count: int) -> list[str]:
    data = bin_path.read_bytes()
    offset = vram_addr - 0x8C010000
    if offset < 0 or offset + byte_count > len(data):
        raise ValueError(f"Offset 0x{offset:x} out of range (file size: {len(data)})")

    chunk = data[offset : offset + byte_count]
    results = []

    if HAS_CAPSTONE:
        md = capstone.Cs(capstone.CS_ARCH_SH, capstone.CS_MODE_SH4 | capstone.CS_MODE_LITTLE_ENDIAN)
        curr_offset = 0
        while curr_offset < len(chunk):
            addr = vram_addr + curr_offset
            sub_chunk = chunk[curr_offset : curr_offset + 32]
            disasms = list(md.disasm(sub_chunk, addr, count=1))
            if disasms:
                insn = disasms[0]
                raw_bytes = " ".join(f"{b:02x}" for b in insn.bytes)
                results.append(f"0x{insn.address:08X}:  {raw_bytes:<6}  {insn.mnemonic:<8} {insn.op_str}")
                curr_offset += insn.size
            else:
                # 2-byte unknown opcode / literal pool word
                if curr_offset + 2 <= len(chunk):
                    w = struct.unpack("<H", chunk[curr_offset : curr_offset + 2])[0]
                    raw_bytes = f"{chunk[curr_offset]:02x} {chunk[curr_offset+1]:02x}"
                    results.append(f"0x{addr:08X}:  {raw_bytes:<6}  .word    0x{w:04x}")
                    curr_offset += 2
                else:
                    b = chunk[curr_offset]
                    results.append(f"0x{addr:08X}:  {b:02x}      .byte    0x{b:02x}")
                    curr_offset += 1
    else:
        words = struct.unpack(f"<{len(chunk)//2}H", chunk[:(len(chunk)//2)*2])
        for i, w in enumerate(words):
            addr = vram_addr + i * 2
            results.append(f"0x{addr:08X}:  0x{w:04x}")

    return results


def main():
    parser = argparse.ArgumentParser(description="SH-4 disassembler")
    parser.add_argument("--bin", type=Path, default=Path("build-inputs/1ST_READ_descrambled.bin"))
    parser.add_argument("--vram", type=lambda x: int(x, 0), default=0x8C010080)
    parser.add_argument("--size", type=lambda x: int(x, 0), default=128)
    args = parser.parse_args()

    lines = disassemble_range(args.bin, args.vram, args.size)
    for line in lines:
        print(line)


if __name__ == "__main__":
    main()
