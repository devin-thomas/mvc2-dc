#!/usr/bin/env python3
"""SH-4 Target Machine Code Match Verifier for Marvel vs. Capcom 2 (Dreamcast NA).

Compiles candidate C functions using the target Dreamcast toolchain (sh-elf-gcc),
extracts raw .text section bytes, and compares them byte-for-byte against the
reference binary (1ST_READ_descrambled.bin).

Only functions with 100% identical bytes and zero diffs are certified as byte_identical.
Generates immutable proof bundles under build/proofs/{name}.json.

Usage:
    python tools/verify_match.py --all
    python tools/verify_match.py --func func_8C010080
    python tools/verify_match.py --vram 0x8C010080
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from datetime import datetime, timezone, timedelta
from pathlib import Path

try:
    import capstone
    HAS_CAPSTONE = True
except ImportError:
    HAS_CAPSTONE = False

REPO_ROOT = Path(__file__).resolve().parent.parent
REFERENCE_BIN = REPO_ROOT / "build-inputs" / "1ST_READ_descrambled.bin"
WORKLIST_PATH = REPO_ROOT / "config" / "worklist.json"
SYS_INIT_C = REPO_ROOT / "src" / "sys_init.c"
PROOFS_DIR = REPO_ROOT / "build" / "proofs"
TEMP_DIR = REPO_ROOT / "build" / "verify_temp"

BASE_VRAM = 0x8C010000
WSL_DISTRO = "Dreamcast-Debian12"
WSL_GCC = "/opt/toolchains/dc/sh-elf/bin/sh-elf-gcc"
WSL_OBJCOPY = "/opt/toolchains/dc/sh-elf/bin/sh-elf-objcopy"
WSL_OBJDUMP = "/opt/toolchains/dc/sh-elf/bin/sh-elf-objdump"


def win_to_wsl_path(path: Path) -> str:
    resolved = path.resolve()
    drive = resolved.drive.replace(":", "").lower()
    parts = list(resolved.parts[1:])
    return f"/mnt/{drive}/" + "/".join(parts)


def check_toolchain() -> tuple[bool, str]:
    try:
        res = subprocess.run(
            ["wsl", "-d", WSL_DISTRO, WSL_GCC, "--version"],
            capture_output=True,
            text=True,
            timeout=10
        )
        if res.returncode == 0:
            version_line = res.stdout.splitlines()[0] if res.stdout else "sh-elf-gcc"
            return (True, version_line)
        return (False, f"sh-elf-gcc returned exit code {res.returncode}: {res.stderr}")
    except Exception as e:
        return (False, str(e))


def get_reference_slice(vram: int, size: int) -> bytes:
    if not REFERENCE_BIN.exists():
        raise FileNotFoundError(f"Reference binary {REFERENCE_BIN} not found")
    data = REFERENCE_BIN.read_bytes()
    offset = vram - BASE_VRAM
    if offset < 0 or offset + size > len(data):
        raise ValueError(f"Slice out of bounds: vram=0x{vram:08X}, size={size}, bin_size={len(data)}")
    return data[offset : offset + size]


def extract_candidate_source(name: str, vram: int) -> str | None:
    if not SYS_INIT_C.exists():
        return None
    content = SYS_INIT_C.read_text(encoding="utf-8", errors="replace")
    
    # Locate function definition in sys_init.c
    type_kw = r"(?:void|int|int8_t|uint8_t|int16_t|uint16_t|int32_t|uint32_t|u8|s8|u16|s16|u32|s32|bool|float|double|char|\w+\s*\*|\w+)"
    pattern = re.compile(
        rf"(/\*\s*\n\s*\*\s*0x{vram:08X}[^\n]*\n\s*\*/\s*(?:(?:static|inline|extern)\s+)*{type_kw}\s+(?:{name}|func_{vram:08x}|sub_{vram:08x})\s*\([^)]*\)\s*\{{[^}}]*\}})",
        re.IGNORECASE | re.DOTALL
    )
    m = pattern.search(content)
    if m:
        return m.group(1)

    # Secondary pattern without header
    pattern2 = re.compile(
        rf"((?:(?:static|inline|extern)\s+)*{type_kw}\s+(?:{name}|func_{vram:08x}|sub_{vram:08x})\s*\([^)]*\)\s*\{{[^}}]*\}})",
        re.IGNORECASE | re.DOTALL
    )
    m2 = pattern2.search(content)
    if m2:
        return m2.group(1)

    return None


def disassemble_bytes(raw_bytes: bytes, base_addr: int = 0) -> list[str]:
    if not HAS_CAPSTONE:
        return [f"+0x{i:04X}: {b:02X}" for i, b in enumerate(raw_bytes)]
    md = capstone.Cs(capstone.CS_ARCH_SH, capstone.CS_MODE_SH4 | capstone.CS_MODE_LITTLE_ENDIAN)
    lines = []
    for insn in md.disasm(raw_bytes, base_addr):
        raw_hex = " ".join(f"{b:02X}" for b in insn.bytes)
        lines.append(f"0x{insn.address:08X}: {raw_hex:<10} {insn.mnemonic:<8} {insn.op_str}")
    return lines


def verify_candidate(name: str, vram: int, size: int, c_code: str | None = None) -> dict:
    if c_code is None:
        c_code = extract_candidate_source(name, vram)

    if not c_code:
        return {
            "name": name,
            "vram": f"0x{vram:08X}",
            "size": size,
            "status": "missing_source",
            "byte_identical": False,
            "diff_summary": "No candidate C source found in src/sys_init.c",
        }

    now_iso = datetime.now(timezone(timedelta(hours=-5))).strftime('%Y-%m-%dT%H:%M:%S-05:00')
    ref_bytes = get_reference_slice(vram, size)
    ref_hash = hashlib.sha256(ref_bytes).hexdigest()
    src_hash = hashlib.sha256(c_code.encode()).hexdigest()

    TEMP_DIR.mkdir(parents=True, exist_ok=True)
    PROOFS_DIR.mkdir(parents=True, exist_ok=True)

    src_file = TEMP_DIR / f"{name}.c"
    obj_file = TEMP_DIR / f"{name}.o"
    bin_file = TEMP_DIR / f"{name}.bin"

    # Wrap standalone C file with required headers
    full_c = f"""#include "types.h"
#include "sh4.h"
#include "shinobi.h"
#include "mvc2/input.h"
#include <stdint.h>
#include <stddef.h>

{c_code}
"""
    src_file.write_text(full_c, encoding="utf-8")

    wsl_src = win_to_wsl_path(src_file)
    wsl_obj = win_to_wsl_path(obj_file)
    wsl_bin = win_to_wsl_path(bin_file)
    wsl_inc = win_to_wsl_path(REPO_ROOT / "include")

    flags = ["-c", "-O2", "-m4-single", "-ml", f"-I{wsl_inc}", "-o", wsl_obj, wsl_src]
    compile_cmd = ["wsl", "-d", WSL_DISTRO, WSL_GCC] + flags

    try:
        comp_res = subprocess.run(compile_cmd, capture_output=True, text=True, timeout=15)
        if comp_res.returncode != 0:
            proof = {
                "name": name,
                "vram": f"0x{vram:08X}",
                "size": size,
                "status": "compile_error",
                "byte_identical": False,
                "compiler": "sh-elf-gcc (GCC) 13.2.0",
                "compiler_flags": " ".join(flags),
                "compiler_stderr": comp_res.stderr.strip()[:1000],
                "diff_summary": "Compilation failed under sh-elf-gcc",
                "source_sha256": src_hash,
                "verified_at": now_iso,
            }
            (PROOFS_DIR / f"{name}.json").write_text(json.dumps(proof, indent=2), encoding="utf-8")
            return proof

        # Extract .text binary
        objcopy_cmd = ["wsl", "-d", WSL_DISTRO, WSL_OBJCOPY, "-O", "binary", "-j", ".text", wsl_obj, wsl_bin]
        copy_res = subprocess.run(objcopy_cmd, capture_output=True, text=True, timeout=10)
        if copy_res.returncode != 0 or not bin_file.exists():
            proof = {
                "name": name,
                "vram": f"0x{vram:08X}",
                "size": size,
                "status": "objcopy_error",
                "byte_identical": False,
                "compiler_stderr": copy_res.stderr.strip()[:500],
                "diff_summary": "Failed to extract .text section via objcopy",
                "verified_at": now_iso,
            }
            (PROOFS_DIR / f"{name}.json").write_text(json.dumps(proof, indent=2), encoding="utf-8")
            return proof

        comp_bytes = bin_file.read_bytes()
        comp_hash = hashlib.sha256(comp_bytes).hexdigest()

        # Compare bytes
        min_len = min(len(ref_bytes), len(comp_bytes))
        mismatches = 0
        first_diff_offset = None
        for i in range(min_len):
            if ref_bytes[i] != comp_bytes[i]:
                mismatches += 1
                if first_diff_offset is None:
                    first_diff_offset = i

        size_diff = abs(len(ref_bytes) - len(comp_bytes))
        total_mismatches = mismatches + size_diff
        is_identical = (total_mismatches == 0 and len(ref_bytes) == len(comp_bytes))

        diff_summary = "0 mismatches (100% BYTE-IDENTICAL MATCH)" if is_identical else (
            f"{total_mismatches} mismatch bytes (ref: {len(ref_bytes)} B, compiled: {len(comp_bytes)} B; first diff at +0x{first_diff_offset:04X})"
            if first_diff_offset is not None else f"Length mismatch (ref: {len(ref_bytes)} B, compiled: {len(comp_bytes)} B)"
        )

        proof = {
            "name": name,
            "vram": f"0x{vram:08X}",
            "size": size,
            "status": "byte_identical" if is_identical else "mismatch",
            "byte_identical": is_identical,
            "compiler": "sh-elf-gcc (GCC) 13.2.0",
            "compiler_flags": " ".join(flags),
            "reference_sha256": ref_hash,
            "compiled_sha256": comp_hash,
            "reference_size": len(ref_bytes),
            "compiled_size": len(comp_bytes),
            "mismatch_count": total_mismatches,
            "first_diff_offset": f"+0x{first_diff_offset:04X}" if first_diff_offset is not None else None,
            "diff_summary": diff_summary,
            "source_sha256": src_hash,
            "verified_at": now_iso,
        }

        # Write proof bundle
        (PROOFS_DIR / f"{name}.json").write_text(json.dumps(proof, indent=2), encoding="utf-8")
        return proof

    except Exception as e:
        proof = {
            "name": name,
            "vram": f"0x{vram:08X}",
            "size": size,
            "status": "exception",
            "byte_identical": False,
            "diff_summary": f"Verification exception: {e}",
            "verified_at": now_iso,
        }
        (PROOFS_DIR / f"{name}.json").write_text(json.dumps(proof, indent=2), encoding="utf-8")
        return proof


def main():
    parser = argparse.ArgumentParser(description="Verify SH-4 decompilation byte match against reference binary")
    parser.add_argument("--all", action="store_true", help="Verify all candidate functions")
    parser.add_argument("--func", type=str, help="Verify specific function name")
    parser.add_argument("--vram", type=str, help="Verify specific function by hex VRAM address")
    parser.add_argument("--json", action="store_true", help="Output JSON results")
    args = parser.parse_args()

    ok, toolchain_info = check_toolchain()
    if not ok:
        print(f"[FATAL] Target compiler toolchain unavailable: {toolchain_info}", file=sys.stderr)
        sys.exit(1)

    print(f"[Toolchain] Verified target compiler: {toolchain_info}")
    worklist = json.loads(WORKLIST_PATH.read_text(encoding="utf-8")) if WORKLIST_PATH.exists() else []

    targets = []
    if args.func:
        targets = [e for e in worklist if e.get("name") == args.func]
    elif args.vram:
        vram_int = int(args.vram, 16)
        targets = [e for e in worklist if e.get("vram") == vram_int]
    elif args.all:
        targets = [e for e in worklist if e.get("status") in ("candidate_syntax_valid", "byte_identical")]
    else:
        # Default to first 5 candidate functions
        candidates = [e for e in worklist if e.get("status") in ("candidate_syntax_valid", "byte_identical")]
        targets = candidates[:5]

    if not targets:
        print("[Verify] No targets found to verify.")
        sys.exit(0)

    print(f"[Verify] Running verification against {len(targets)} function(s)...")
    results = []
    worklist_changed = False

    for t in targets:
        name = t["name"]
        vram = t["vram"]
        size = t["size"]
        print(f"  -> Verifying {name} (0x{vram:08X}, {size} B)...", end=" ", flush=True)
        res = verify_candidate(name, vram, size)
        results.append(res)
        if res.get("byte_identical"):
            print(f"[MATCH! 100% BYTE-IDENTICAL]")
            if t.get("status") != "byte_identical":
                t["status"] = "byte_identical"
                worklist_changed = True
        else:
            print(f"[MISMATCH: {res.get('diff_summary')}]")

    if worklist_changed:
        WORKLIST_PATH.write_text(json.dumps(worklist, indent=2), encoding="utf-8")
        print(f"[Worklist] Updated worklist with verified byte-match statuses.")
        subprocess.run(["python", "tools/progress.py", "--badge", "--sync-dashboard"], check=True)

    if args.json:
        print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
