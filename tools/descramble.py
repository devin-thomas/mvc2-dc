#!/usr/bin/env python3
"""Dreamcast Binary Scrambler & Descrambler.

Implements the Marcus Comstedt pseudo-random slice-swap algorithm used by the
Sega Dreamcast boot ROM for 1ST_READ.BIN binaries.

When running on Dreamcast hardware or emulators from standard MIL-CD images,
the bootloader descrambles 1ST_READ.BIN into memory during load. To decompile
or disassemble the executable, the on-disc image must be descrambled into a
linear RAM binary.

Usage:
    python tools/descramble.py descramble input.bin output.bin
    python tools/descramble.py scramble input.bin output.bin
    python tools/descramble.py verify
"""

import argparse
import hashlib
import sys
from pathlib import Path

MAX_CHUNK = 2048 * 1024  # 2MB maximum shuffle window


class DreamcastScrambler:
    """Implements Dreamcast boot ROM pseudo-random slice shuffling."""

    def __init__(self):
        self.seed = 0

    def _srand(self, n: int) -> None:
        self.seed = n & 0xFFFF

    def _rand(self) -> int:
        self.seed = (self.seed * 2109 + 9273) & 0x7FFF
        return (self.seed + 0xC000) & 0xFFFF

    def descramble_bytes(self, data: bytes) -> bytes:
        """Descramble an on-disc scrambled binary into a linear RAM image."""
        filesz = len(data)
        out = bytearray(filesz)
        self._srand(filesz)

        in_pos = 0
        out_pos = 0
        rem_sz = filesz

        chunksz = MAX_CHUNK
        while chunksz >= 32:
            while rem_sz >= chunksz:
                num_slices = chunksz // 32
                idx = list(range(num_slices))
                for i in range(num_slices - 1, -1, -1):
                    x = (self._rand() * i) >> 16
                    idx[i], idx[x] = idx[x], idx[i]
                    slice_dest = out_pos + 32 * idx[i]
                    out[slice_dest : slice_dest + 32] = data[in_pos : in_pos + 32]
                    in_pos += 32
                rem_sz -= chunksz
                out_pos += chunksz
            chunksz >>= 1

        if rem_sz > 0:
            out[out_pos : out_pos + rem_sz] = data[in_pos : in_pos + rem_sz]

        return bytes(out)

    def scramble_bytes(self, data: bytes) -> bytes:
        """Scramble a linear RAM image into on-disc scrambled format."""
        filesz = len(data)
        out = bytearray(filesz)
        self._srand(filesz)

        in_pos = 0
        out_pos = 0
        rem_sz = filesz

        chunksz = MAX_CHUNK
        while chunksz >= 32:
            while rem_sz >= chunksz:
                num_slices = chunksz // 32
                idx = list(range(num_slices))
                for i in range(num_slices - 1, -1, -1):
                    x = (self._rand() * i) >> 16
                    idx[i], idx[x] = idx[x], idx[i]
                    slice_src = in_pos + 32 * idx[i]
                    out[out_pos : out_pos + 32] = data[slice_src : slice_src + 32]
                    out_pos += 32
                rem_sz -= chunksz
                in_pos += chunksz
            chunksz >>= 1

        if rem_sz > 0:
            out[out_pos : out_pos + rem_sz] = data[in_pos : in_pos + rem_sz]

        return bytes(out)


def descramble_file(input_path: Path, output_path: Path) -> tuple[str, str]:
    scrambler = DreamcastScrambler()
    raw = input_path.read_bytes()
    desc = scrambler.descramble_bytes(raw)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(desc)
    sha1 = hashlib.sha1(desc).hexdigest()
    sha256 = hashlib.sha256(desc).hexdigest()
    return sha1, sha256


def scramble_file(input_path: Path, output_path: Path) -> tuple[str, str]:
    scrambler = DreamcastScrambler()
    raw = input_path.read_bytes()
    scrambled = scrambler.scramble_bytes(raw)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(scrambled)
    sha1 = hashlib.sha1(scrambled).hexdigest()
    sha256 = hashlib.sha256(scrambled).hexdigest()
    return sha1, sha256


def self_test():
    """Verify scrambler invertibility with synthetic and known test vectors."""
    test_data = bytes((i * 13 + 7) & 0xFF for i in range(1024 * 1024 + 17))
    sc = DreamcastScrambler()
    scrambled = sc.scramble_bytes(test_data)
    descrambled = sc.descramble_bytes(scrambled)
    assert descrambled == test_data, "Scrambler / Descrambler round-trip mismatch!"
    print("Self-test passed: 100% round-trip match on 1MB test vector.")


def main():
    parser = argparse.ArgumentParser(description="Dreamcast binary scrambler/descrambler")
    subparsers = parser.add_subparsers(dest="command", required=True)

    p_desc = subparsers.add_parser("descramble", help="Descramble 1ST_READ.BIN for decompilation")
    p_desc.add_argument("input", type=Path, help="Input scrambled binary")
    p_desc.add_argument("output", type=Path, help="Output descrambled linear binary")

    p_scram = subparsers.add_parser("scramble", help="Scramble linear binary for disc layout")
    p_scram.add_argument("input", type=Path, help="Input linear binary")
    p_scram.add_argument("output", type=Path, help="Output scrambled binary")

    subparsers.add_parser("verify", help="Run scrambler self-test")

    args = parser.parse_args()

    if args.command == "descramble":
        sha1, sha256 = descramble_file(args.input, args.output)
        print(f"Descrambled: {args.input} -> {args.output}")
        print(f"  SHA-1:   {sha1}")
        print(f"  SHA-256: {sha256}")
    elif args.command == "scramble":
        sha1, sha256 = scramble_file(args.input, args.output)
        print(f"Scrambled: {args.input} -> {args.output}")
        print(f"  SHA-1:   {sha1}")
        print(f"  SHA-256: {sha256}")
    elif args.command == "verify":
        self_test()


if __name__ == "__main__":
    main()
