#!/usr/bin/env python3
"""Dreamcast Binary Splitter.

Splits descrambled 1ST_READ.BIN into assembly and data segments according to
config/mvc2_na_dc.yaml, producing assembly stubs and linker scripts.

Usage:
    python tools/split.py [config/mvc2_na_dc.yaml]
"""

import argparse
import hashlib
import struct
import sys
from pathlib import Path
import yaml

BASE_VRAM = 0x8C010000


def disassemble_crt0(data: bytes, vram_start: int) -> list[str]:
    """Generate clean assembly for the CRT0 preamble."""
    lines = [
        "! =====================================================================",
        "! Dreamcast CRT0 Startup & Header Preamble",
        f"! VRAM: 0x{vram_start:08X} - 0x{vram_start + len(data):08X}",
        "! =====================================================================",
        "    .section .crt0, \"ax\"",
        "    .little",
        "    .global start",
        "    .global _start",
        "",
        "start:",
        "_start:",
    ]

    # First 6 words are NOPs
    for i in range(6):
        addr = vram_start + i * 2
        lines.append(f"    nop                         ! 0x{addr:08x}: 0x0009")

    lines.append("")
    lines.append("_crt0_entry:")
    # Next instructions: d005 6102 d205 2129 9204 212b ...
    words = struct.unpack(f"<{len(data)//2}H", data)
    for i in range(6, min(16, len(words))):
        addr = vram_start + i * 2
        w = words[i]
        lines.append(f"    .word   0x{w:04x}              ! 0x{addr:08x}")

    # Lib Handle table at 0x30
    if len(data) >= 0x80:
        lines.append("")
        lines.append("    .section .rodata.lib_handles, \"a\"")
        lines.append("    .global _lib_handle_start")
        lines.append("    .global _lib_handle_end")
        lines.append("_lib_handle_start:")
        for off in range(0x30, 0x80, 16):
            chunk = data[off : off + 16]
            ascii_repr = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
            words_hex = ", ".join(f"0x{w:08x}" for w in struct.unpack("<4I", chunk))
            lines.append(f"    .long   {words_hex}  ! 0x{vram_start + off:08x} \"{ascii_repr}\"")
        lines.append("_lib_handle_end:")

    return lines


def split_binary(config_path: Path):
    with open(config_path, "r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f)

    target_path = Path(cfg["options"]["target_path"])
    if not target_path.exists():
        print(f"[!] Error: Target binary not found: {target_path}", file=sys.stderr)
        print("    Please run `make setup` or `python tools/setup_disc.py` first.", file=sys.stderr)
        sys.exit(1)

    data = target_path.read_bytes()
    expected_sha1 = cfg.get("sha1")
    actual_sha1 = hashlib.sha1(data).hexdigest()
    if expected_sha1 and actual_sha1 != expected_sha1:
        print(f"[!] Warning: Target binary SHA-1 mismatch!", file=sys.stderr)
        print(f"    Expected: {expected_sha1}", file=sys.stderr)
        print(f"    Actual:   {actual_sha1}", file=sys.stderr)

    asm_dir = Path(cfg["options"].get("asm_path", "asm"))
    data_dir = Path(cfg["options"].get("asset_path", "assets"))
    asm_dir.mkdir(parents=True, exist_ok=True)
    data_dir.mkdir(parents=True, exist_ok=True)

    print(f"[*] Splitting {target_path} ({len(data):,} bytes) using {config_path}...")

    # Flatten subsegments
    subsegments = []
    segments = cfg["segments"]
    for seg in segments:
        if isinstance(seg, dict) and "subsegments" in seg:
            vram_base = seg.get("vram", BASE_VRAM)
            seg_start = seg.get("start", 0)
            for sub in seg["subsegments"]:
                subsegments.append({
                    "start": sub[0],
                    "type": sub[1],
                    "name": sub[2],
                    "vram": vram_base + (sub[0] - seg_start),
                })
        elif isinstance(seg, list) and len(seg) == 1:
            end_offset = seg[0]

    # Sort subsegments by start offset
    subsegments.sort(key=lambda x: x["start"])

    linker_objects = []

    for i, sub in enumerate(subsegments):
        start = sub["start"]
        end = subsegments[i + 1]["start"] if i + 1 < len(subsegments) else len(data)
        chunk = data[start:end]
        name = sub["name"]
        stype = sub["type"]
        vram = sub["vram"]

        if name == "crt0":
            out_file = asm_dir / f"{name}.s"
            lines = disassemble_crt0(chunk, vram)
            out_file.write_text("\n".join(lines) + "\n", encoding="utf-8")
            print(f"    [CRT0] 0x{start:08x}..0x{end:08x} -> {out_file} ({len(chunk)} bytes)")
        elif stype == "asm":
            out_file = asm_dir / f"{name}.s"
            # Write stub with incbin fallback for matching
            bin_chunk_file = data_dir / f"{name}.bin"
            bin_chunk_file.write_bytes(chunk)
            lines = [
                f"! Segment: {name} (VRAM: 0x{vram:08x} - 0x{vram + len(chunk):08x})",
                f"    .section .text.{name}, \"ax\"",
                "    .little",
                f"    .global {name}_start",
                f"{name}_start:",
                f"    .incbin \"{bin_chunk_file.as_posix()}\"",
            ]
            out_file.write_text("\n".join(lines) + "\n", encoding="utf-8")
            print(f"    [ASM]  0x{start:08x}..0x{end:08x} -> {out_file} ({len(chunk):,} bytes)")
        else:
            out_bin = data_dir / f"{name}.bin"
            out_bin.write_bytes(chunk)
            out_file = asm_dir / f"{name}.s"
            lines = [
                f"! Data Segment: {name} (VRAM: 0x{vram:08x} - 0x{vram + len(chunk):08x})",
                f"    .section .{stype}.{name}, \"a\"",
                f"    .global {name}_data",
                f"{name}_data:",
                f"    .incbin \"{out_bin.as_posix()}\"",
            ]
            out_file.write_text("\n".join(lines) + "\n", encoding="utf-8")
            print(f"    [DATA] 0x{start:08x}..0x{end:08x} -> {out_file} ({len(chunk):,} bytes)")

        linker_objects.append(out_file)

    # Generate linker script
    ld_path = asm_dir / "linker.ld"
    ld_lines = [
        "/* Marvel vs. Capcom 2 (Dreamcast NA) Linker Script */",
        "OUTPUT_FORMAT(\"elf32-shl\", \"elf32-shl\", \"elf32-shl\")",
        "OUTPUT_ARCH(sh)",
        "ENTRY(start)",
        "",
        "MEMORY",
        "{",
        "    ram (rwx) : ORIGIN = 0x8C010000, LENGTH = 16M",
        "}",
        "",
        "SECTIONS",
        "{",
        "    . = 0x8C010000;",
        "    .crt0 : { *(.crt0) } > ram",
        "    .rodata.lib_handles : { *(.rodata.lib_handles) } > ram",
        "    .text : { *(.text*) } > ram",
        "    .rodata : { *(.rodata*) } > ram",
        "    .data : { *(.data*) } > ram",
        "    .bss : { *(.bss*) *(COMMON) } > ram",
        "}",
    ]
    ld_path.write_text("\n".join(ld_lines) + "\n", encoding="utf-8")
    print(f"[OK] Split complete. Generated linker script: {ld_path}")


def main():
    parser = argparse.ArgumentParser(description="Split descrambled Dreamcast 1ST_READ.BIN")
    parser.add_argument("config", nargs="?", default="config/mvc2_na_dc.yaml", type=Path)
    args = parser.parse_args()
    split_binary(args.config)


if __name__ == "__main__":
    main()
