---
type: implementation-plan
id: "003"
date: "2026-10-17"
title: "Decompilation Orchestrator Integrity Audit & Recovery Implementation Plan"
status: approved
---

# Decompilation Orchestrator Integrity Audit & Recovery Implementation Plan

## 1. Overview & Baseline Snapshot (Freeze Point)
Following the receipt of `MVC2_DC_Qwen_Orchestrator_Audit_and_Recovery_2026-10-17.md`, the autonomous decompilation pipeline has been frozen at Wave 173 to address critical verification flaws.

### Baseline Snapshot at Freeze Point:
- **Git HEAD SHA**: `e85d35f7c23d3b53244b47302d5747f31f152b1c`
- **Checkpoint Tag / Branch**: `audit/pre-verification-2026-10-17-e85d35f`
- **Completed Waves**: Wave 173 (terminated cleanly via `config/STOP`)
- **Supervisor / Daemon State**: Halted. No autonomous worker is modifying files or Git.
- **Cluster Hosts**: `research` (M5 Max 36GB, `llama-server:11435`) and `command` (M5 Air 24GB, `ollama:11434`) healthy and available for controlled verification testing.
- **Legacy Worklist State (`config/worklist.json`)**:
  - Total entries: 1,946
  - `matched` (legacy label): 1,552 entries (200,984 bytes claimed, ~15.335%)
  - `stubbed`: 197 entries
  - `pending`: 197 entries
- **Source Diagnostic (`src/sys_init.c`)**:
  - Address headers: 1,745
  - Stubs: 197
  - Nonempty function definitions: **32** (the remaining 1,520 headers contain blank/comment-only snippets that passed `-fsyntax-only` without emitting function definitions).
- **Certified Byte-Identical Code Bytes**: **Not yet established** (0 bytes proven against reference binary).

---

## 2. Core Flaws Diagnosed

1. **Syntax Check Conflated with Byte-Matching**: The daemon promoted candidates directly to `status: "matched"` if Clang `-fsyntax-only` exited 0. No target compiler was invoked and no byte-level diff was performed.
2. **Fail-Open Validator**: `validate_c_syntax()` caught exceptions with `except Exception as e: return (True, str(e))`, permitting false positives on unexpected crashes.
3. **Empty Source Accepted**: Header-only or comment-only snippets passed `-fsyntax-only`, leading to 1,520 empty headers in `src/sys_init.c`.
4. **Label-Based Accounting**: `tools/progress.py` summed byte sizes solely from `f["status"] == "matched"` without verifying that real machine-code bytes match the target binary.

---

## 3. Implementation Phases

### Phase 1: Stop False Metrics & Harden Validator
1. Patch `validate_c_syntax()` to **fail closed** on all errors.
2. Implement **strict AST/definition validation**: reject any candidate lacking a non-empty C function definition corresponding to the target function name/address.
3. Replace binary `matched` status with an auditable orthogonal state model:
   - `candidate_syntax_valid`: Valid C definition that passes syntax check.
   - `target_compiled`: Successfully compiles for target SH-4 ABI.
   - `byte_identical`: Certified 100% byte-matched against reference binary slice.
   - `byte_mismatch`: Compiles but machine code diverges.
   - `stubbed`: Marked for manual analysis.
4. Overhaul `tools/progress.py` to independently compute:
   - Queue / Discovery coverage (triaged candidates).
   - Generated C coverage (syntactically valid definitions, labeled as estimates).
   - Verified matching coverage (sum of only `byte_identical` functions with proof).
5. Add regression test suite under `tests/` verifying rejection of empty bodies, comments, syntax errors, and missing definitions.

### Phase 2: Historical Output Audit & Migration
1. Deterministically audit all 1,946 entries in `config/worklist.json` and `src/sys_init.c`.
2. Extract the genuine non-empty C function definitions (~32 routines) into a verified candidate pool.
3. Reset unpopulated address headers to `pending` or `legacy_empty`.
4. Publish a transparent audit report and migration manifest.

### Phase 3: Grounded Matching & Diff Runner
1. Verify target SH-4 toolchain and exact flags.
2. Build `tools/verify_match.py`: extracts reference binary slice, compiles candidate C function to object file, disassembles/diffs, and produces an immutable proof receipt.
3. Exercise the diff runner against known leaf routines to establish the first genuine byte-matched functions.

### Phase 4: Controlled Qwen Pipeline
1. Re-enable autonomous Qwen workers only as **candidate generators**.
2. Candidates must pass AST validation $\to$ syntax check $\to$ SH-4 compilation $\to$ diff runner before any byte-match claim is made.
3. Update public dashboards, badges, and notifications to reflect honest, multi-dimensional metrics.
