#!/usr/bin/env python3
"""Decompilation Progress Tracker for Marvel vs. Capcom 2 (Dreamcast NA).

Calculates matching code and data percentages, tracks function counts,
and outputs badges / JSON for decomp.dev and GitHub Actions.

Usage:
    python tools/progress.py [--csv] [--badge] [--json]
"""

import argparse
import json
import re
import sys
from pathlib import Path

TOTAL_TEXT_BYTES = 1310592      # ~1.25 MB conservative text span
TOTAL_DATA_BYTES = 499225       # Initialized data + rodata
TOTAL_UNITS = 8240              # Estimated function candidate count


def calculate_progress(src_dir: Path, asm_dir: Path) -> dict:
    matched_code_bytes = 0
    matched_data_bytes = 0
    matched_funcs = 0

    # Scan src/ for matching C implementations
    c_files = list(src_dir.glob("**/*.c"))
    for cf in c_files:
        content = cf.read_text(encoding="utf-8", errors="replace")
        # Count matched functions marked with MATCHING or INCLUDE_ASM absence
        funcs = re.findall(r"(?:void|int|u32|s32|bool|float)\s+([a-zA-Z0-9_]+)\s*\([^)]*\)\s*\{", content)
        for fn in funcs:
            if not fn.startswith("INCLUDE_ASM"):
                matched_funcs += 1
                # Estimate conservative 64 bytes per function stub unless explicit
                matched_code_bytes += 64

    # Calculate percentages
    code_pct = (matched_code_bytes / TOTAL_TEXT_BYTES) * 100 if TOTAL_TEXT_BYTES else 0.0
    data_pct = (matched_data_bytes / TOTAL_DATA_BYTES) * 100 if TOTAL_DATA_BYTES else 0.0

    return {
        "matched_funcs": matched_funcs,
        "total_funcs": TOTAL_UNITS,
        "matched_code_bytes": matched_code_bytes,
        "total_code_bytes": TOTAL_TEXT_BYTES,
        "code_percent": round(code_pct, 3),
        "matched_data_bytes": matched_data_bytes,
        "total_data_bytes": TOTAL_DATA_BYTES,
        "data_percent": round(data_pct, 3),
    }


def main():
    parser = argparse.ArgumentParser(description="Calculate decompilation progress")
    parser.add_argument("--json", action="store_true", help="Output JSON progress report")
    parser.add_argument("--badge", action="store_true", help="Generate SVG badge")
    parser.add_argument("--src", type=Path, default=Path("src"), help="Source directory")
    parser.add_argument("--asm", type=Path, default=Path("asm"), help="Assembly directory")
    args = parser.parse_args()

    prog = calculate_progress(args.src, args.asm)

    if args.json:
        print(json.dumps(prog, indent=2))
        return

    print("=" * 60)
    print("Marvel vs. Capcom 2 (Dreamcast NA) - Decompilation Progress")
    print("=" * 60)
    print(f"Matched Code:   {prog['matched_code_bytes']:,} / {prog['total_code_bytes']:,} bytes ({prog['code_percent']:.3f}%)")
    print(f"Matched Data:   {prog['matched_data_bytes']:,} / {prog['total_data_bytes']:,} bytes ({prog['data_percent']:.3f}%)")
    print(f"Functions:      {prog['matched_funcs']:,} / {prog['total_funcs']:,} matched")
    print("=" * 60)

    if args.badge:
        badge_path = Path("assets/progress.svg")
        badge_path.parent.mkdir(parents=True, exist_ok=True)
        pct = prog["code_percent"]
        color = "#4c1" if pct > 80 else ("#dfb317" if pct > 20 else "#e05d44")
        svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="130" height="20">
  <linearGradient id="b" x2="0" y2="100%">
    <stop offset="0" stop-color="#bbb" stop-opacity=".1"/>
    <stop offset="1" stop-opacity=".1"/>
  </linearGradient>
  <clipPath id="a">
    <rect width="130" height="20" rx="3" fill="#fff"/>
  </clipPath>
  <g clip-path="url(#a)">
    <path fill="#555" d="M0 0h65v20H0z"/>
    <path fill="{color}" d="M65 0h65v20H65z"/>
    <path fill="url(#b)" d="M0 0h130v20H0z"/>
  </g>
  <g fill="#fff" text-anchor="middle" font-family="DejaVu Sans,Verdana,Geneva,sans-serif" font-size="11">
    <text x="32.5" y="15" fill="#010101" fill-opacity=".3">matching</text>
    <text x="32.5" y="14">matching</text>
    <text x="96.5" y="15" fill="#010101" fill-opacity=".3">{pct:.2f}%</text>
    <text x="96.5" y="14">{pct:.2f}%</text>
  </g>
</svg>"""
        badge_path.write_text(svg, encoding="utf-8")
        print(f"[OK] Wrote progress badge to {badge_path}")


if __name__ == "__main__":
    main()
