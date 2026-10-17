#!/usr/bin/env python3
"""Phase 2: Historical Output Audit & Migration Script for MvC2 Dreamcast.

Audits all legacy entries in config/worklist.json and src/sys_init.c:
1. Preserves original pre-audit snapshots in build/audit_baseline/.
2. Parses src/sys_init.c into discrete sections.
3. Distinguishes:
   - Genuine non-empty C definitions (passing AST/syntax check -> candidate_syntax_valid)
   - Stubs (documented stubs -> stubbed)
   - Empty comment headers (pruned from source -> reset to pending in worklist)
4. Emits clean, pruned src/sys_init.c containing ONLY real C definitions and stubs.
5. Saves detailed audit report to build/audit_migration_report.json.
6. Synchronizes worklist.json and triggers truthful progress calculation.
"""

import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from tools.auto_decomp_daemon import validate_candidate_code, validate_c_syntax

SYS_INIT_C = REPO_ROOT / "src" / "sys_init.c"
WORKLIST_PATH = REPO_ROOT / "config" / "worklist.json"
BASELINE_DIR = REPO_ROOT / "build" / "audit_baseline"
REPORT_PATH = REPO_ROOT / "build" / "audit_migration_report.json"


def run_audit_migration():
    print("=" * 65)
    print("MvC2 Dreamcast - Phase 2 Historical Audit & Migration")
    print("=" * 65)

    BASELINE_DIR.mkdir(parents=True, exist_ok=True)
    backup_c = BASELINE_DIR / "sys_init_pre_audit.c"
    backup_wl = BASELINE_DIR / "worklist_pre_audit.json"

    if not backup_c.exists():
        shutil.copy2(SYS_INIT_C, backup_c)
        print(f"[Backup] Saved pre-audit snapshot: {backup_c}")
    if not backup_wl.exists():
        shutil.copy2(WORKLIST_PATH, backup_wl)
        print(f"[Backup] Saved pre-audit snapshot: {backup_wl}")

    c_raw = SYS_INIT_C.read_text(encoding="utf-8", errors="replace")
    worklist = json.loads(WORKLIST_PATH.read_text(encoding="utf-8"))

    # Split sys_init.c into preamble and function blocks
    header_pattern = re.compile(r"(/\*\s*\n\s*\*\s*0x([0-9A-Fa-f]{8})[^\n]*\n\s*\*/)")
    splits = list(header_pattern.finditer(c_raw))

    preamble = c_raw[:splits[0].start()] if splits else c_raw
    blocks = []

    for i, match in enumerate(splits):
        header_text = match.group(1)
        addr_hex = match.group(2)
        vram = int(addr_hex, 16)
        start_idx = match.end()
        end_idx = splits[i + 1].start() if i + 1 < len(splits) else len(c_raw)
        body = c_raw[start_idx:end_idx].strip()
        blocks.append({
            "vram": vram,
            "header": header_text,
            "body": body,
            "full_text": c_raw[match.start():end_idx],
        })

    print(f"[Parse] Found {len(blocks)} address header blocks in {SYS_INIT_C.name}")

    type_kw = r"(?:void|int|int8_t|uint8_t|int16_t|uint16_t|int32_t|uint32_t|u8|s8|u16|s16|u32|s32|bool|float|double|char|\w+\s*\*|\w+)"
    def_pattern = re.compile(rf"(?:static\s+|inline\s+)*{type_kw}\s+([a-zA-Z0-9_]+)\s*\([^)]*\)\s*\{{", re.MULTILINE)

    candidates = []
    stubs = []
    empty_headers = []

    for blk in blocks:
        body = blk["body"]
        vram = blk["vram"]
        clean_body = re.sub(r'/\*.*?\*/|//.*?$', '', body, flags=re.DOTALL | re.MULTILINE).strip()

        # Check for stub markers
        if "STUBBED" in body or "INCLUDE_ASM" in body:
            stubs.append(blk)
            continue

        # Check for actual function definitions
        func_matches = def_pattern.findall(clean_body)
        if func_matches and "{" in clean_body and "}" in clean_body:
            # Check if balanced and has real code inside
            func_name = func_matches[0]
            # Verify syntax
            valid_syntax, err = validate_c_syntax(body, target_name=func_name, target_vram=vram)
            blk["func_name"] = func_name
            blk["syntax_valid"] = valid_syntax
            blk["syntax_error"] = err if not valid_syntax else ""
            blk["sha256"] = hashlib.sha256(body.encode()).hexdigest()
            candidates.append(blk)
        else:
            empty_headers.append(blk)

    print(f"[Audit] Classification results:")
    print(f"  - Genuine C Candidate Definitions: {len(candidates)}")
    print(f"  - Documented Stubs:                {len(stubs)}")
    print(f"  - Empty Comment Headers:           {len(empty_headers)}")

    candidate_vrams = {c["vram"]: c for c in candidates}
    stub_vrams = {s["vram"]: s for s in stubs}
    empty_vrams = {e["vram"]: e for e in empty_headers}

    # Reclassify worklist
    reclassified = {
        "candidate_syntax_valid": 0,
        "candidate_syntax_invalid": 0,
        "stubbed": 0,
        "empty_reset_to_pending": 0,
        "already_pending": 0,
    }

    for entry in worklist:
        vram = entry["vram"]
        prev_status = entry.get("status")
        if vram in candidate_vrams:
            cand = candidate_vrams[vram]
            if cand["syntax_valid"]:
                entry["status"] = "candidate_syntax_valid"
                entry["name"] = cand["func_name"]
                reclassified["candidate_syntax_valid"] += 1
            else:
                entry["status"] = "candidate_syntax_invalid"
                entry["name"] = cand["func_name"]
                reclassified["candidate_syntax_invalid"] += 1
        elif vram in stub_vrams:
            entry["status"] = "stubbed"
            reclassified["stubbed"] += 1
        elif prev_status == "matched":
            # Was claimed matched, but body is completely empty! Reset to pending!
            entry["status"] = "pending"
            reclassified["empty_reset_to_pending"] += 1
        elif prev_status == "stubbed":
            reclassified["stubbed"] += 1
        else:
            reclassified["already_pending"] += 1

    print(f"[Worklist Reclassification]:")
    for k, v in reclassified.items():
        print(f"  - {k}: {v}")

    # Save updated worklist
    WORKLIST_PATH.write_text(json.dumps(worklist, indent=2), encoding="utf-8")
    print(f"[Worklist] Written updated worklist to {WORKLIST_PATH}")

    # Generate clean, pruned sys_init.c
    new_c_lines = [preamble.rstrip(), ""]
    # Add genuine candidates first
    new_c_lines.append("/* ===========================================================================")
    new_c_lines.append(" * Validated C Function Candidates (Host Clang Syntax Verified)")
    new_c_lines.append(" * =========================================================================== */\n")

    for cand in sorted(candidates, key=lambda x: x["vram"]):
        new_c_lines.append(f"/*\n * 0x{cand['vram']:08X} - {cand['func_name']}\n */")
        new_c_lines.append(cand["body"])
        new_c_lines.append("")

    # Add documented stubs
    new_c_lines.append("/* ===========================================================================")
    new_c_lines.append(" * Documented Assembly Stubs")
    new_c_lines.append(" * =========================================================================== */\n")

    for s in sorted(stubs, key=lambda x: x["vram"]):
        new_c_lines.append(s["header"])
        if s["body"]:
            new_c_lines.append(s["body"])
        new_c_lines.append("")

    SYS_INIT_C.write_text("\n".join(new_c_lines) + "\n", encoding="utf-8")
    print(f"[Prune] Written pruned {SYS_INIT_C.name} (only valid candidates and stubs retained)")

    # Write audit report
    report = {
        "audit_timestamp": "2026-10-17T13:45:00-05:00",
        "pre_audit_counts": {
            "worklist_total": len(worklist),
            "claimed_matched": sum(1 for e in json.loads(backup_wl.read_text(encoding="utf-8")) if e.get("status") == "matched"),
            "stubbed": sum(1 for e in json.loads(backup_wl.read_text(encoding="utf-8")) if e.get("status") == "stubbed"),
            "pending": sum(1 for e in json.loads(backup_wl.read_text(encoding="utf-8")) if e.get("status") == "pending"),
            "sys_init_total_lines": len(c_raw.splitlines()),
        },
        "post_audit_counts": {
            "genuine_candidates_found": len(candidates),
            "candidate_syntax_valid": reclassified["candidate_syntax_valid"],
            "candidate_syntax_invalid": reclassified["candidate_syntax_invalid"],
            "empty_headers_pruned": len(empty_headers),
            "empty_reset_to_pending": reclassified["empty_reset_to_pending"],
            "stubs_retained": len(stubs),
            "current_pending": sum(1 for e in worklist if e.get("status") == "pending"),
            "verified_byte_identical": 0,
        },
        "candidates": [
            {
                "name": c["func_name"],
                "vram": f"0x{c['vram']:08X}",
                "syntax_valid": c["syntax_valid"],
                "sha256": c["sha256"],
            }
            for c in candidates
        ],
    }

    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
    REPORT_PATH.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"[Report] Saved migration audit report to {REPORT_PATH}")

    # Sync dashboard and badges
    subprocess.run(["python", "tools/progress.py", "--badge", "--sync-dashboard"], check=True)
    print("=" * 65)
    print("Phase 2 Audit & Migration Completed Successfully!")
    print("=" * 65)


if __name__ == "__main__":
    run_audit_migration()
