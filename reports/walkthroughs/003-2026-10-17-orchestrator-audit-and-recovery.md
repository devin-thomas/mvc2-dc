---
type: walkthrough
id: "003"
date: "2026-10-17"
title: "P0 Integrity Audit, Validation Hardening, and Historical Decompilation Recovery"
status: executed
---

# Walkthrough 003: P0 Integrity Audit, Validation Hardening, and Historical Decompilation Recovery

## Executive Summary

Following receipt of the P0 audit and recovery specification (`MVC2_DC_Qwen_Orchestrator_Audit_and_Recovery_2026-10-17.md`), the autonomous Marvel vs. Capcom 2 Dreamcast decompilation loop was safely frozen and audited.

The audit revealed that prior to this intervention, the autonomous loop treated Clang host syntax validation (`-fsyntax-only`) as byte-match proof (`status = "matched"`), failed open on compiler exceptions, and allowed blank/comment-only snippets to count as matched code. As a result, the dashboard previously claimed **15.418% matched (1,569 functions, 202,072 bytes)** when only **26 actual non-empty C definitions** existed in `src/sys_init.c`, **1,522 entries were empty comment headers**, and **0 bytes were verified byte-identical against the reference binary**.

All four recovery phases have now been executed:
1. **Phase 0 (Freeze & Baseline Snapshot)**: Clean supervisor stop at Wave 173; checkpoint tag `audit/pre-verification-2026-10-17-e85d35f` created. Pre-audit artifacts preserved in `build/audit_baseline/`.
2. **Phase 1 (Validation Hardening & Multi-Tier Metrics)**: Fail-closed compiler validation, AST definition checks rejecting empty/comment/declaration-only snippets, truthful three-tier metrics (Verified Byte-Match, Generated Candidates, Queue Triage), and a 14-fixture regression test suite in `tests/test_validation.py` (100% passing).
3. **Phase 2 (Historical Audit & Pruning)**: `tools/audit_migration.py` executed: 1,522 empty headers pruned from `src/sys_init.c` (reduced from 11,281 bloated lines to 1,818 lines of real C code and stubs); 1,527 phantom entries reset to `pending` in `config/worklist.json`; 25 candidate C definitions verified.
4. **Phase 3 (Ground Matching Harness)**: `tools/verify_match.py` created using target `sh-elf-gcc (GCC) 13.2.0` via WSL `Dreamcast-Debian12`, extracting `.text` bytes and generating cryptographic proof bundles under `build/proofs/`.
5. **Clean Shutdown**: Background dashboard server and daemons safely halted; working tree clean.

---

## Metrics Comparison: Truthful Before vs. After

| Metric | Before Audit (Wave 173 Claim) | After Audit & Migration (Ground Truth) | Classification |
| :--- | :--- | :--- | :--- |
| **Verified Byte-Identical Matching** | `15.418%` *(false claim)* | **`0.000%` (0 / 1,310,592 bytes)** | Ground Truth (Diff Runner Proof) |
| **Verified Byte-Identical Functions** | `1,569` *(false claim)* | **`0` functions** | Ground Truth (Diff Runner Proof) |
| **Generated C Candidates (Syntax Valid)** | Unmeasured | **`25` functions (~2,152 bytes est., 0.164%)** | Host Syntax Valid Definitions |
| **Documented Assembly Stubs** | `197` functions | **`197` functions** | Preserved Stubs |
| **Empty Comment-Only Headers** | `1,522` headers *(counted as matched!)* | **`0` headers (1,522 pruned from source)** | Purged from C source |
| **Queue Triage (Pending Functions)** | `197` pending | **`1,724` pending functions** | Real work queue for decompiler |
| **Total Scanned Worklist Units** | `1,946` functions | **`1,946` functions (denom: 8,240 est.)** | Worklist Queue Coverage |
| **Legacy Claimed Metric** | Displaced reality | **Preserved under `legacy_claimed` (15.3%)** | Explicit provisional disclaimer |

---

## Implemented Tooling & Verification Assets

1. **Candidate Validator & Hardened Reviewer**:
   - File: [`tools/auto_decomp_daemon.py`](file:///c:/dev/personal/mvc2-dc/tools/auto_decomp_daemon.py)
   - `validate_candidate_code()`: Strips comments, verifies balanced braces, checks for genuine C function definition, enforces target name / address alias match.
   - `validate_c_syntax()`: Fails closed on all compiler exceptions and timeouts.
   - Never promotes to `matched`: sets `status: "candidate_syntax_valid"`.

2. **Truthful Multi-Tier Progress Tracker**:
   - File: [`tools/progress.py`](file:///c:/dev/personal/mvc2-dc/tools/progress.py)
   - Independently calculates verified byte-matches, candidate definitions, queue status, and legacy provisional metrics.
   - Syncs badge to `assets/progress.svg` (`verified | 0.000%`), `status.json`, `STATUS.md`, and `index.html`.

3. **Automated Integrity Test Suite (14 Fixtures)**:
   - File: [`tests/test_validation.py`](file:///c:/dev/personal/mvc2-dc/tests/test_validation.py)
   - Tested and verified:
     - Fixture 1: Empty model output rejected.
     - Fixture 2: Comments-only / address header rejected.
     - Fixture 3: Prototype or extern-only rejected.
     - Fixture 4: C syntactically invalid rejected.
     - Fixture 5: Clang missing/timeout fails closed.
     - Fixture 6: Valid host C candidate only, never verified match.
     - Fixture 7: Valid compile with different bytes is mismatch.
     - Fixture 8: Slice offset / address range boundary checks.
     - Fixture 9: Literal pool detection at function tail.
     - Fixture 10: No double counting on duplicate functions.
     - Fixture 11: Stubbed functions not counted verified.
     - Fixture 12: Legacy entry with no body flagged as legacy-only.
     - Fixture 13: Proof invalidated upon C source modification.
     - Fixture 14: Model descriptive name provisional without evidence.
   - Result: `Ran 14 tests in 0.076s - OK`.

4. **Historical Audit & Migration**:
   - File: [`tools/audit_migration.py`](file:///c:/dev/personal/mvc2-dc/tools/audit_migration.py)
   - Report: [`reports/audit_migration_report_2026-10-17.json`](file:///c:/dev/personal/mvc2-dc/reports/audit_migration_report_2026-10-17.json)
   - Pre-audit snapshots preserved in `build/audit_baseline/`.

5. **SH-4 Target Machine Code Match Verifier**:
   - File: [`tools/verify_match.py`](file:///c:/dev/personal/mvc2-dc/tools/verify_match.py)
   - Compiler: `sh-elf-gcc (GCC) 13.2.0` (in WSL `Dreamcast-Debian12`).
   - Flags: `-c -O2 -m4-single -ml -Iinclude`.
   - Extracts `.text` section via `sh-elf-objcopy`, compares against `build-inputs/1ST_READ_descrambled.bin`, and generates cryptographic proof bundles in `build/proofs/{name}.json`.

---

## Clean Shutdown Verification

- All background tasks halted: task `task-1088` (HTTP server on port 4182) terminated.
- No running python or supervisor processes for `mvc2-dc`.
- Working tree clean and all migration work committed on branch `qwen`.
