#!/usr/bin/env python3
"""Byte-for-byte differential matching validator.

Compares built binary sections or full linked output against the descrambled
retail reference binary, reporting matching percentage and first discrepancies.

Usage:
    python tools/check_match.py build/1ST_READ.BIN build-inputs/1ST_READ_descrambled.bin
"""

import argparse
import hashlib
import sys
from pathlib import Path


def compare_binaries(built_path: Path, expected_path: Path) -> bool:
    if not built_path.exists():
        print(f"[!] Error: Built binary not found: {built_path}", file=sys.stderr)
        return False
    if not expected_path.exists():
        print(f"[!] Error: Reference binary not found: {expected_path}", file=sys.stderr)
        return False

    built = built_path.read_bytes()
    expected = expected_path.read_bytes()

    built_sha1 = hashlib.sha1(built).hexdigest()
    expected_sha1 = hashlib.sha1(expected).hexdigest()

    print("=" * 65)
    print("Differential Verification Check")
    print("=" * 65)
    print(f"Built File:     {built_path} ({len(built):,} bytes)")
    print(f"  SHA-1:        {built_sha1}")
    print(f"Reference File: {expected_path} ({len(expected):,} bytes)")
    print(f"  SHA-1:        {expected_sha1}")
    print("-" * 65)

    if built_sha1 == expected_sha1:
        print("[MATCH] 100.0% EXACT BYTE-FOR-BYTE MATCH!")
        return True

    # Identify discrepancies
    min_len = min(len(built), len(expected))
    diff_count = sum(1 for a, b in zip(built[:min_len], expected[:min_len]) if a != b)
    diff_count += abs(len(built) - len(expected))

    match_pct = ((min_len - diff_count) / max(len(built), len(expected))) * 100.0
    print(f"[MISMATCH] Matching: {match_pct:.4f}% ({diff_count:,} differing bytes)")

    # Print first 5 differences
    printed = 0
    for idx in range(min_len):
        if built[idx] != expected[idx]:
            addr = 0x8C010000 + idx
            print(f"  Diff at offset 0x{idx:08X} (VRAM 0x{addr:08X}): built 0x{built[idx]:02X} != expected 0x{expected[idx]:02X}")
            printed += 1
            if printed >= 5:
                break

    return False


def main():
    parser = argparse.ArgumentParser(description="Check matching binary against reference")
    parser.add_argument("built", type=Path, help="Path to compiled/relinked binary")
    parser.add_argument("reference", nargs="?", default="build-inputs/1ST_READ_descrambled.bin", type=Path, help="Path to reference binary")
    args = parser.parse_args()

    matches = compare_binaries(args.built, args.reference)
    sys.exit(0 if matches else 1)


if __name__ == "__main__":
    main()
