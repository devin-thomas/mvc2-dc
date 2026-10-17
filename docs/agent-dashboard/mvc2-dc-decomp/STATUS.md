# Task Status: Marvel vs. Capcom 2 (Dreamcast) Autonomous Decompilation

- **Task ID**: `mvc2-dc-decomp-20261016`
- **Updated At**: `2026-10-17T13:00:37-05:00`
- **Overall State**: `working`
- **Phase**: `Phase 2: Multi-Host Decompilation Loop & Peripheral Subsystems`
- **Repository / Branch**: `https://github.com/devin-thomas/mvc2-dc` (`qwen`)
- **Dashboard URL**: [https://titan.takaya-lionfish.ts.net:8472/](https://titan.takaya-lionfish.ts.net:8472/) (Tailscale Serve)

---

## Action Required
> **No action required from you right now.**
> The autonomous multi-host decompilation daemon is active across the local Apple Silicon cluster (`research` + `command`).

---

## Current Activity
Decompiling Dreamcast MK-51049: **1289/8240** functions (**13.184%**, 172,794 bytes) matched across cluster.

---

## Milestones

| Milestone | Status |
| :--- | :--- |
| **Milestone 1**: Environment, Tooling & Binary Extraction | `complete` |
| **Milestone 2**: Multi-Node AI Cluster Pipeline (`ai-pipe` unbuffered) | `complete` |
| **Milestone 3**: Autonomous Queue Daemon & Compiler Reviewer Loop | `complete` |
| **Milestone 4**: Maple Bus & Peripheral Subsystems (`src/sys_init.c`) | `in_progress` |
| **Milestone 5**: Ninja Graphics & Rend Engine Decompilation | `pending` |
| **Milestone 6**: Gameplay & Fighter State Tables | `pending` |
| **Milestone 7**: Byte-Match Verification (`objdiff` / 100% target) | `pending` |

---

## Blockers
*None currently.*

---

## Warnings
- **[Agent handling]**: `research` host internal SSD space is low (~43 GB); all repos, tools, and build outputs are strictly restricted to `/Volumes/PortableSSD/mvc2-dc`.

---

## Decisions
1. **Route functions by size**: $\le 120$ bytes $\to$ `command` (Qwen 9B), $> 120$ bytes $\to$ `research` (Qwen 27B). *Final*
2. **Incorporate Clang syntax-only check** (`clang -fsyntax-only -I include`) as automated reviewer. *Final*
3. **Private Tailscale Serve over port 8472** for cross-device monitoring. *Final*

---

## Key Outputs
- Immutable Reports Ledger: [`reports/INDEX.md`](../../../reports/INDEX.md)
- Plan 001: [`reports/plans/001-2026-10-16-dreamcast-decompilation-cluster.md`](../../../reports/plans/001-2026-10-16-dreamcast-decompilation-cluster.md)
- Walkthrough 001: [`reports/walkthroughs/001-2026-10-16-cluster-and-first-functions.md`](../../../reports/walkthroughs/001-2026-10-16-cluster-and-first-functions.md)
- Decompiled System Code: [`src/sys_init.c`](../../../src/sys_init.c)
- Autonomous Daemon: [`tools/auto_decomp_daemon.py`](../../../tools/auto_decomp_daemon.py)
- Live Stream Output: [`build/qwen_live.md`](../../../build/qwen_live.md)

---

## Next Steps
1. Run autonomous daemon batch on remaining startup and Maple bus handlers.
2. Identify and label literal pools vs instruction streams in disassembler.
3. Extract Ninja graphics initialization functions.
4. Update progress badge and `objdiff` configurations.

---

## Walk-Away Snapshot
- **Last verified good state**: All 11 functions syntax-checked with Clang, cluster reachable, daemon operational.
- **Unfinished operation**: Batch processing functions in `0x8C010400` - `0x8C011000` range.
- **Next safe step**: `python tools/auto_decomp_daemon.py --batch 5`
- **Touched files**: `src/sys_init.c`, `tools/auto_decomp_daemon.py`, `reports/INDEX.md`, `docs/agent-dashboard/mvc2-dc-decomp/status.json`
- **Expected running processes**: Python HTTP server (`127.0.0.1:4182`), Tailscale Serve (`https://titan.takaya-lionfish.ts.net:8472`)
