#!/usr/bin/env python3
"""Dreamcast Binary Inspector for Marvel vs. Capcom 2.

Analyzes raw or descrambled Dreamcast executables, inspecting:
- Scramble state (scrambled on-disc vs descrambled linear RAM image)
- Shinobi SDK, Kamui2, and CRI middleware signatures
- Base load address and CRT0 entry sequence
- Verification against known retail North American fingerprints

Usage:
    python tools/inspect_dc.py path/to/1ST_READ.BIN
"""

import argparse
import hashlib
import re
import struct
import sys
from pathlib import Path

# Ground truth retail fingerprints for North American Marvel vs. Capcom 2 (MK-51049)
EXPECTED_FINGERPRINTS = {
    "scrambled": {
        "size": 1809817,
        "sha1": "0655030b9ea9ba88f94640320391163e663aff3d",
        "sha256": "591c328d225f7b84eaae361730d46dbcf372e92e5b0de2ee81c674f4aa878f5f",
    },
    "descrambled": {
        "size": 1809817,
        "sha1": "d792ef936edd89dfb2ce5399c6b4949460dd3d7b",
        "sha256": "c24708540c018bc01728be117e3d405e1e91add1f0f975793eab52086298463e",
    },
}


def analyze_binary(path: Path) -> dict:
    if not path.exists():
        raise FileNotFoundError(f"File not found: {path}")

    data = path.read_bytes()
    size = len(data)
    sha1 = hashlib.sha1(data).hexdigest()
    sha256 = hashlib.sha256(data).hexdigest()

    # Detect scramble state
    is_descrambled = False
    is_scrambled = False

    if sha1 == EXPECTED_FINGERPRINTS["descrambled"]["sha1"]:
        is_descrambled = True
    elif sha1 == EXPECTED_FINGERPRINTS["scrambled"]["sha1"]:
        is_scrambled = True
    else:
        # Check first 12 bytes for 6x NOP (0x0009)
        if len(data) >= 12 and data[:12] == b"\x09\x00\x09\x00\x09\x00\x09\x00\x09\x00\x09\x00":
            is_descrambled = True
        else:
            is_scrambled = True

    # Scan for middleware strings
    middleware = []
    signatures = [
        rb"Shinobi Library for Dreamcast Version [0-9\.]+",
        rb"Shinobi Ver [0-9\.]+ Build:[^\x00\r\n]+",
        rb"kamui2 Ver [0-9,]+ Build:[^\x00\r\n]+",
        rb"kmdk Ver [0-9,]+ Build:[^\x00\r\n]+",
        rb"ADXT Ver\.[0-9\.]+ Build:[^\x00\r\n]+",
        rb"ADXF Ver\.[0-9\.]+ Build:[^\x00\r\n]+",
        rb"mwRna Ver\.[0-9\.]+ Build:[^\x00\r\n]+",
        rb"mdmAPI Ver\. [0-9\.]+ Build:[^\x00\r\n]+",
        rb"modem Ver [0-9\.]+ Build:[^\x00\r\n]+",
        rb"syStart Ver [0-9\.]+ Build:[^\x00\r\n]+",
        rb"gdFs Ver [0-9\.]+ Build:[^\x00\r\n]+",
        rb"pd Ver [0-9\.]+ Build:[^\x00\r\n]+",
        rb"pdKbd Ver [0-9\.]+ Build:[^\x00\r\n]+",
        rb"bu Ver [0-9\.]+ Build:[^\x00\r\n]+",
    ]

    for pattern in signatures:
        for match in re.finditer(pattern, data):
            raw_text = match.group(0).decode("latin1", errors="replace").strip()
            # Keep only printable ascii
            clean_text = "".join(c if 32 <= ord(c) < 127 else " " for c in raw_text).strip()
            if clean_text and clean_text not in middleware:
                middleware.append(clean_text)

    # Disassemble initial 8 instructions
    instructions = []
    if len(data) >= 32:
        words = struct.unpack(f"<{min(16, len(data) // 2)}H", data[: min(32, len(data))])
        for i, w in enumerate(words):
            addr = 0x8C010000 + i * 2
            instructions.append((addr, w))

    return {
        "path": path,
        "size": size,
        "sha1": sha1,
        "sha256": sha256,
        "state": "descrambled (RAM image)" if is_descrambled else "scrambled (Disc track)",
        "matches_retail_scrambled": sha1 == EXPECTED_FINGERPRINTS["scrambled"]["sha1"],
        "matches_retail_descrambled": sha1 == EXPECTED_FINGERPRINTS["descrambled"]["sha1"],
        "middleware": middleware,
        "instructions": instructions,
    }


def main():
    parser = argparse.ArgumentParser(description="Dreamcast binary inspector")
    parser.add_argument("binary", type=Path, help="Path to 1ST_READ.BIN")
    args = parser.parse_args()

    info = analyze_binary(args.binary)

    print("=" * 72)
    print(f"Target Binary: {info['path']} ({info['size']:,} bytes)")
    print("=" * 72)
    print(f"Format State:  {info['state']}")
    print(f"SHA-1:         {info['sha1']}")
    print(f"SHA-256:       {info['sha256']}")

    if info["matches_retail_descrambled"]:
        print("Verification:  [MATCH] Pristine NTSC-U Retail Descrambled RAM Executable")
    elif info["matches_retail_scrambled"]:
        print("Verification:  [MATCH] Pristine NTSC-U Retail Scrambled Disc Executable")
        print("               Run `make descramble` or `python tools/descramble.py descramble` to unpack.")
    else:
        print("Verification:  [NOTE] Hash does not match pristine NTSC-U retail baseline.")

    if info["middleware"]:
        print("\nEmbedded SDK & Middleware Signatures:")
        for m in info["middleware"]:
            print(f"  * {m}")

    print("\nInitial Entry Words (Base 0x8C010000):")
    for addr, word in info["instructions"]:
        print(f"  0x{addr:08x}: 0x{word:04x}")


if __name__ == "__main__":
    main()
