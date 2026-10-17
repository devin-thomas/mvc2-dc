#!/usr/bin/env python3
"""Decompilation Progress Tracker for Marvel vs. Capcom 2 (Dreamcast NA).

Calculates matching code and data percentages across three distinct tiers:
1. Verified Byte-Identical Matching: Proven identical machine code via SH-4 diff runner.
2. Generated C Candidate Coverage: Syntactically valid definitions (estimated bytes).
3. Queue / Discovery Coverage: Scanned and triaged function candidates.
4. Legacy Claimed: Preserved historical metrics with explicit provisional disclaimer.

Outputs badges and JSON for agent dashboard and reporting.

Usage:
    python tools/progress.py [--badge] [--sync-dashboard] [--json]
"""

import argparse
import json
import re
import sys
from pathlib import Path

TOTAL_TEXT_BYTES = 1310592      # ~1.25 MB conservative text span
TOTAL_DATA_BYTES = 499225       # Initialized data + rodata
TOTAL_UNITS = 8240              # Estimated function candidate count
PROOFS_DIR = Path("build/proofs")
WORKLIST_FILE = Path("config/worklist.json")


def calculate_progress(src_dir: Path = Path("src"), asm_dir: Path = Path("asm")) -> dict:
    verified_matched_funcs = 0
    verified_matched_bytes = 0
    verified_func_names = set()

    # 1. Scan verified proofs
    if PROOFS_DIR.exists():
        for pf in PROOFS_DIR.glob("*.json"):
            try:
                pdata = json.loads(pf.read_text(encoding="utf-8"))
                if pdata.get("byte_identical") is True:
                    verified_matched_funcs += 1
                    verified_matched_bytes += pdata.get("size", 0)
                    verified_func_names.add(pdata.get("name"))
            except Exception:
                pass

    # 2. Inspect worklist
    worklist_entries = []
    candidate_funcs = 0
    candidate_code_bytes = 0
    candidate_names = set()

    legacy_funcs = 0
    legacy_code_bytes = 0

    pending_funcs = 0
    stubbed_funcs = 0

    if WORKLIST_FILE.exists():
        try:
            worklist_entries = json.loads(WORKLIST_FILE.read_text(encoding="utf-8"))
            for entry in worklist_entries:
                st = entry.get("status")
                name = entry.get("name")
                size = entry.get("size", 64)
                if st == "byte_identical":
                    if name not in verified_func_names:
                        verified_matched_funcs += 1
                        verified_matched_bytes += size
                        verified_func_names.add(name)
                elif st in ("candidate_syntax_valid", "candidate"):
                    candidate_funcs += 1
                    candidate_code_bytes += size
                    candidate_names.add(name)
                elif st == "legacy_claimed":
                    legacy_funcs += 1
                    legacy_code_bytes += size
                elif st == "stubbed":
                    stubbed_funcs += 1
                elif st == "pending":
                    pending_funcs += 1
                elif st == "matched":
                    # Pre-audit legacy entries without diff proof
                    if name not in verified_func_names:
                        legacy_funcs += 1
                        legacy_code_bytes += size
        except Exception:
            pass

    # 3. Scan src/ for actual non-empty C definitions
    if src_dir.exists():
        type_kw = r"(?:void|int|int8_t|uint8_t|int16_t|uint16_t|int32_t|uint32_t|u8|s8|u16|s16|u32|s32|bool|float|double|char|\w+\s*\*|\w+)"
        func_pattern = re.compile(rf"(?:static\s+|inline\s+)*{type_kw}\s+([a-zA-Z0-9_]+)\s*\([^)]*\)\s*\{{", re.MULTILINE)
        for cf in src_dir.glob("**/*.c"):
            content = cf.read_text(encoding="utf-8", errors="replace")
            clean = re.sub(r'/\*.*?\*/|//.*?$', '', content, flags=re.DOTALL | re.MULTILINE)
            for m in func_pattern.finditer(clean):
                fn = m.group(1)
                if fn.startswith("INCLUDE_ASM") or fn in verified_func_names or fn in candidate_names:
                    continue
                candidate_funcs += 1
                candidate_code_bytes += 64
                candidate_names.add(fn)

    triaged_funcs = len(worklist_entries)
    verified_pct = (verified_matched_bytes / TOTAL_TEXT_BYTES) * 100 if TOTAL_TEXT_BYTES else 0.0
    candidate_pct = (candidate_code_bytes / TOTAL_TEXT_BYTES) * 100 if TOTAL_TEXT_BYTES else 0.0
    legacy_pct = (legacy_code_bytes / TOTAL_TEXT_BYTES) * 100 if TOTAL_TEXT_BYTES else 0.0
    queue_pct = (triaged_funcs / TOTAL_UNITS) * 100 if TOTAL_UNITS else 0.0

    return {
        "verified_matched_funcs": verified_matched_funcs,
        "verified_matched_bytes": verified_matched_bytes,
        "verified_code_percent": round(verified_pct, 3),

        "candidate_funcs": candidate_funcs,
        "candidate_code_bytes": candidate_code_bytes,
        "candidate_code_percent": round(candidate_pct, 3),

        "triaged_funcs": triaged_funcs,
        "pending_funcs": pending_funcs,
        "stubbed_funcs": stubbed_funcs,
        "total_funcs": TOTAL_UNITS,
        "queue_coverage_percent": round(queue_pct, 3),

        "legacy_claimed_funcs": legacy_funcs,
        "legacy_claimed_bytes": legacy_code_bytes,
        "legacy_claimed_percent": round(legacy_pct, 3),
        "legacy_disclaimer": "PROVISIONAL PIPELINE STATE: Pre-audit metric based on host syntax checks without AST function body validation or SH-4 binary diff proof.",

        "matched_data_bytes": 0,
        "total_data_bytes": TOTAL_DATA_BYTES,
        "data_percent": 0.0,
        "total_code_bytes": TOTAL_TEXT_BYTES,

        # Backwards compatibility
        "matched_funcs": verified_matched_funcs,
        "matched_code_bytes": verified_matched_bytes,
        "code_percent": round(verified_pct, 3),
    }


def sync_dashboard(prog: dict):
    dashboard_dir = Path("docs/agent-dashboard/mvc2-dc-decomp")
    status_json = dashboard_dir / "status.json"
    status_md = dashboard_dir / "STATUS.md"
    index_html = dashboard_dir / "index.html"

    if not status_json.exists():
        return

    from datetime import datetime, timezone, timedelta
    now_iso = datetime.now(timezone(timedelta(hours=-5))).strftime('%Y-%m-%dT%H:%M:%S-05:00')

    try:
        data = json.loads(status_json.read_text(encoding="utf-8"))
        data["updated_at"] = now_iso
        data["current_activity"] = (
            f"Decompiling Dreamcast T1212N: {prog['verified_matched_funcs']}/{prog['total_funcs']} verified byte-identical "
            f"({prog['verified_code_percent']:.3f}%), {prog['candidate_funcs']} syntax-valid C candidates ({prog['candidate_code_percent']:.3f}% est.), "
            f"{prog['pending_funcs']} pending."
        )
        data["verified_matching"] = {
            "functions": prog["verified_matched_funcs"],
            "bytes": prog["verified_matched_bytes"],
            "total_bytes": prog["total_code_bytes"],
            "percent": prog["verified_code_percent"]
        }
        data["candidate_coverage"] = {
            "functions": prog["candidate_funcs"],
            "estimated_bytes": prog["candidate_code_bytes"],
            "percent": prog["candidate_code_percent"]
        }
        data["queue_status"] = {
            "triaged": prog["triaged_funcs"],
            "pending": prog["pending_funcs"],
            "stubbed": prog["stubbed_funcs"],
            "total_estimated": prog["total_funcs"],
            "percent": prog["queue_coverage_percent"]
        }
        data["legacy_provisional"] = {
            "claimed_functions": prog["legacy_claimed_funcs"],
            "claimed_bytes": prog["legacy_claimed_bytes"],
            "claimed_percent": prog["legacy_claimed_percent"],
            "disclaimer": prog["legacy_disclaimer"]
        }
        if "walk_away_snapshot" in data:
            data["walk_away_snapshot"]["last_good_state"] = (
                f"Verified: {prog['verified_matched_funcs']} byte-identical ({prog['verified_code_percent']:.3f}%); "
                f"Candidates: {prog['candidate_funcs']} C definitions ({prog['candidate_code_percent']:.3f}% est.)"
            )
        status_json.write_text(json.dumps(data, indent=2), encoding="utf-8")
        print(f"[Dashboard] Updated {status_json} (Verified: {prog['verified_code_percent']:.3f}%, Candidates: {prog['candidate_funcs']})")
    except Exception as e:
        print(f"[Dashboard Error] Failed to update status.json: {e}", file=sys.stderr)

    if index_html.exists():
        try:
            html = index_html.read_text(encoding="utf-8")
            html = re.sub(r'name="dashboard-updated-at" content="[^"]*"', f'name="dashboard-updated-at" content="{now_iso}"', html)
            html = re.sub(r'<span id="updated-at" class="timestamp">[^<]*</span>', f'<span id="updated-at" class="timestamp">{now_iso}</span>', html)
            pct_str = f"{prog['verified_code_percent']:.3f}%"
            activity_html = (
                f'<p id="current-activity">Decompiling Dreamcast T1212N: '
                f'<strong>{prog["verified_matched_funcs"]}/{prog["total_funcs"]}</strong> verified byte-identical '
                f'(<strong>{pct_str}</strong>, {prog["verified_matched_bytes"]:,} bytes) | '
                f'<strong>{prog["candidate_funcs"]}</strong> C candidates generated ({prog["candidate_code_percent"]:.3f}% est.) | '
                f'<strong>{prog["pending_funcs"]}</strong> pending queue items.</p>'
            )
            html = re.sub(r'<p id="current-activity">.*?</p>', activity_html, html, flags=re.DOTALL)
            index_html.write_text(html, encoding="utf-8")
        except Exception as e:
            print(f"[Dashboard Error] Failed to update index.html: {e}", file=sys.stderr)

    if status_md.exists():
        try:
            md = status_md.read_text(encoding="utf-8")
            md = re.sub(r'- \*\*Updated At\*\*: `[^`]*`', f'- **Updated At**: `{now_iso}`', md)
            activity_md = (
                f"## Current Activity\n"
                f"Decompiling Dreamcast T1212N:\n"
                f"- **Verified Byte-Identical**: **{prog['verified_matched_funcs']}/{prog['total_funcs']}** functions (**{prog['verified_code_percent']:.3f}%**, {prog['verified_matched_bytes']:,} bytes)\n"
                f"- **Generated C Candidates**: **{prog['candidate_funcs']}** definitions (**{prog['candidate_code_percent']:.3f}%** est., {prog['candidate_code_bytes']:,} bytes)\n"
                f"- **Queue / Triage**: **{prog['triaged_funcs']}/{prog['total_funcs']}** candidates triaged (**{prog['pending_funcs']}** pending, **{prog['stubbed_funcs']}** stubbed)\n"
                f"- **Legacy Provisional**: **{prog['legacy_claimed_funcs']}** functions claimed ({prog['legacy_claimed_percent']:.3f}% provisional before audit)\n\n"
            )
            md = re.sub(r'## Current Activity\n.*?\n\n', activity_md, md, flags=re.DOTALL)
            status_md.write_text(md, encoding="utf-8")
        except Exception as e:
            print(f"[Dashboard Error] Failed to update STATUS.md: {e}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description="Calculate decompilation progress")
    parser.add_argument("--json", action="store_true", help="Output JSON progress report")
    parser.add_argument("--badge", action="store_true", help="Generate SVG badge")
    parser.add_argument("--sync-dashboard", action="store_true", help="Sync progress to agent dashboard")
    parser.add_argument("--src", type=Path, default=Path("src"), help="Source directory")
    parser.add_argument("--asm", type=Path, default=Path("asm"), help="Assembly directory")
    args = parser.parse_args()

    prog = calculate_progress(args.src, args.asm)

    if args.json:
        print(json.dumps(prog, indent=2))
        return

    print("=" * 65)
    print("Marvel vs. Capcom 2 (Dreamcast NA) - Decompilation Progress")
    print("=" * 65)
    print(f"Verified Byte-Match: {prog['verified_matched_bytes']:,} / {prog['total_code_bytes']:,} bytes ({prog['verified_code_percent']:.3f}%) [{prog['verified_matched_funcs']} funcs]")
    print(f"Generated Candidates:{prog['candidate_code_bytes']:,} / {prog['total_code_bytes']:,} bytes ({prog['candidate_code_percent']:.3f}% est.) [{prog['candidate_funcs']} funcs]")
    print(f"Queue Triage:        {prog['triaged_funcs']:,} / {prog['total_funcs']:,} funcs ({prog['pending_funcs']} pending, {prog['stubbed_funcs']} stubbed)")
    if prog['legacy_claimed_funcs'] > 0:
        print(f"Legacy Claimed:      {prog['legacy_claimed_funcs']:,} funcs ({prog['legacy_claimed_percent']:.3f}% provisional before audit)")
        print(f"  * Disclaimer: {prog['legacy_disclaimer']}")
    print("=" * 65)

    if args.badge:
        badge_path = Path("assets/progress.svg")
        badge_path.parent.mkdir(parents=True, exist_ok=True)
        pct = prog["verified_code_percent"]
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
    <text x="32.5" y="15" fill="#010101" fill-opacity=".3">verified</text>
    <text x="32.5" y="14">verified</text>
    <text x="96.5" y="15" fill="#010101" fill-opacity=".3">{pct:.3f}%</text>
    <text x="96.5" y="14">{pct:.3f}%</text>
  </g>
</svg>"""
        badge_path.write_text(svg, encoding="utf-8")
        print(f"[OK] Wrote progress badge to {badge_path}")

    if args.sync_dashboard:
        sync_dashboard(prog)


if __name__ == "__main__":
    main()
