#!/usr/bin/env python3
"""Automated setup script for Marvel vs. Capcom 2 (Dreamcast NA).

Extracts and verifies disc assets from a user-supplied CDI or GDI disc image,
descrambles 1ST_READ.BIN to linear memory layout, and verifies matching hashes.

Usage:
    python tools/setup_disc.py --cdi path/to/game.cdi
    python tools/setup_disc.py --bin path/to/1ST_READ.BIN [--descrambled]
"""

import argparse
import hashlib
import sys
from pathlib import Path

from cdi_extract import DreamcastCDI
from descramble import DreamcastScrambler, descramble_file

EXPECTED_SCRAMBLED_SHA1 = "0655030b9ea9ba88f94640320391163e663aff3d"
EXPECTED_DESCRAMBLED_SHA1 = "d792ef936edd89dfb2ce5399c6b4949460dd3d7b"
EXPECTED_IP_BIN_SHA1 = "bd2b774a6cb07211672de282ae82d7fbc242f660"


def setup_from_cdi(cdi_path: Path, output_dir: Path) -> bool:
    print(f"[*] Opening Dreamcast CDI image: {cdi_path}")
    cdi = DreamcastCDI(cdi_path)
    track = cdi.get_data_track()

    extracted_dir = output_dir / "extracted"
    extracted_dir.mkdir(parents=True, exist_ok=True)

    # 1. Extract IP.BIN
    print("[*] Extracting boot sector (IP.BIN)...")
    ip_bin = cdi.get_ip_bin()
    ip_path = extracted_dir / "IP.BIN"
    ip_path.write_bytes(ip_bin)
    ip_sha1 = hashlib.sha1(ip_bin).hexdigest()
    print(f"    IP.BIN: {len(ip_bin):,} bytes (SHA-1: {ip_sha1})")
    if ip_sha1 == EXPECTED_IP_BIN_SHA1:
        print("    -> [OK] IP.BIN matches North American retail release (MK-51049).")
    else:
        print(f"    -> [WARN] IP.BIN does not match expected {EXPECTED_IP_BIN_SHA1}.")

    # 2. Extract 1ST_READ.BIN
    print("[*] Locating 1ST_READ.BIN in data track...")
    files = cdi.list_files()
    target_info = next((f for f in files if f["name"].upper() == "1ST_READ.BIN"), None)
    if not target_info:
        print("[!] ERROR: 1ST_READ.BIN not found in CDI filesystem!", file=sys.stderr)
        return False

    raw_bin = cdi.read_track_bytes(track, target_info["rel_lba"], target_info["size"])
    scrambled_path = extracted_dir / "1ST_READ.BIN"
    scrambled_path.write_bytes(raw_bin)
    scrambled_sha1 = hashlib.sha1(raw_bin).hexdigest()
    print(f"    Scrambled 1ST_READ.BIN: {len(raw_bin):,} bytes (SHA-1: {scrambled_sha1})")

    if scrambled_sha1 == EXPECTED_SCRAMBLED_SHA1:
        print("    -> [OK] On-disc binary matches North American retail baseline.")
    else:
        print(f"    -> [WARN] Hash does not match expected {EXPECTED_SCRAMBLED_SHA1}.")

    # 3. Descramble to RAM layout
    print("[*] Descrambling 1ST_READ.BIN into linear memory layout (base 0x8C010000)...")
    descrambled_path = output_dir / "1ST_READ_descrambled.bin"
    desc_sha1, desc_sha256 = descramble_file(scrambled_path, descrambled_path)
    print(f"    Descrambled binary: {descrambled_path.stat().st_size:,} bytes")
    print(f"    SHA-1:   {desc_sha1}")
    print(f"    SHA-256: {desc_sha256}")

    if desc_sha1 == EXPECTED_DESCRAMBLED_SHA1:
        print("    -> [MATCH] Exact match with retail NTSC-U linear loaded image!")
        print("\n[SUCCESS] Setup complete. Ready for `make split` and decompilation.")
        return True
    else:
        print(f"[!] ERROR: Descrambled hash {desc_sha1} does not match expected {EXPECTED_DESCRAMBLED_SHA1}")
        return False


def setup_from_bin(bin_path: Path, output_dir: Path, is_descrambled: bool) -> bool:
    data = bin_path.read_bytes()
    sha1 = hashlib.sha1(data).hexdigest()
    output_dir.mkdir(parents=True, exist_ok=True)

    if is_descrambled or sha1 == EXPECTED_DESCRAMBLED_SHA1:
        dest = output_dir / "1ST_READ_descrambled.bin"
        dest.write_bytes(data)
        print(f"[OK] Staged descrambled binary to {dest} (SHA-1: {sha1})")
        return sha1 == EXPECTED_DESCRAMBLED_SHA1
    else:
        scrambled_path = output_dir / "extracted" / "1ST_READ.BIN"
        scrambled_path.parent.mkdir(parents=True, exist_ok=True)
        scrambled_path.write_bytes(data)
        descrambled_path = output_dir / "1ST_READ_descrambled.bin"
        desc_sha1, _ = descramble_file(scrambled_path, descrambled_path)
        print(f"[OK] Descrambled {bin_path} -> {descrambled_path} (SHA-1: {desc_sha1})")
        return desc_sha1 == EXPECTED_DESCRAMBLED_SHA1


def main():
    parser = argparse.ArgumentParser(description="Setup disc assets for mvc2-dc decompilation")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--cdi", type=Path, help="Path to retail Dreamcast CDI disc image")
    group.add_argument("--bin", type=Path, help="Path to existing 1ST_READ.BIN file")
    parser.add_argument("--descrambled", action="store_true", help="Supplied --bin is already descrambled")
    parser.add_argument("--dest", type=Path, default=Path("build-inputs"), help="Output directory (default: build-inputs)")
    args = parser.parse_args()

    if args.cdi:
        success = setup_from_cdi(args.cdi, args.dest)
    else:
        success = setup_from_bin(args.bin, args.dest, args.descrambled)

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
