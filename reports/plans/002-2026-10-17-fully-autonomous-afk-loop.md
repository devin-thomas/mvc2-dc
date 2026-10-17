---
type: implementation-plan
id: "002"
date: "2026-10-17"
title: "Fully Autonomous AFK Decompilation Loop & Reliability Hardening"
status: draft
---

# Fully Autonomous AFK Decompilation Loop & Reliability Hardening

## 1. Objective & Success Criteria
Transition the Marvel vs. Capcom 2 (Dreamcast NA) decompilation pipeline from supervised batch execution to a resilient, self-healing, 100% autonomous background daemon that operates completely unattended (AFK) across the local cluster (`research` M5 Max 36GB + `command` M5 Air 24GB).

### Key Targets:
- **Zero Token Cost**: Strictly local models (6 parallel slots of Qwen3.8-27B on `research:11435`, 1 slot of Qwen3.5-9B on `command:11434`).
- **Zero Deadlocks**: Graceful 3-attempt timeout with compiler syntax feedback, falling back to stubbing difficult functions so the queue never halts.
- **Dynamic Worklist Expansion**: Automatic ingestion of the entire ~1.25 MB SH-4 `.text` section (`0x8C010000` to `0x8C150000`, ~8,240 functions) in sequential chunks.
- **Atomic Commits & State Sync**: Thread-safe commits to Git, automatic progress calculation (`tools/progress.py`), and real-time dashboard updates (`status.json`, `progress.svg`).
- **Self-Healing Supervisor Loop**: A top-level watchdog wrapper script that restarts the daemon on transient network errors (with exponential backoff) and only halts if reference binary integrity or physical disk space is compromised.

---

## 2. Cluster Architecture & Concurrency Model

```
+-----------------------------------------------------------------------------------+
|                                 Titan (Host)                                      |
|                                                                                   |
|  +-----------------------------------------------------------------------------+  |
|  |             auto_decomp_daemon.py (Multi-Worker ThreadPool)                 |  |
|  +-----------------------------------------------------------------------------+  |
|         |           |           |           |           |           |        |    |
|      Worker 0    Worker 1    Worker 2    Worker 3    Worker 4    Worker 5  Worker 6   |
+---------|-----------|-----------|-----------|-----------|-----------|--------|----+
          |           |           |           |           |           |        |
          |           |   (Tailscale HTTP :11435)                 |        | (Tailscale HTTP :11434)
          v           v           v           v           v           v        v
+---------------------------------------------------------------------+  +-------------+
|                     Research (Apple M5 Max, 36GB)                   |  |   Command   |
|                                                                     |  |   (M5 Air)  |
|  llama-server (-np 6, -c 49152, --flash-attn auto, Metal GPU)       |  |  Ollama     |
|  Model: hf.co/unsloth/Qwen3.8-27B-GGUF:UD-Q4_K_M (18.6 GB VRAM)    |  |  qwen3.5:9b |
+---------------------------------------------------------------------+  +-------------+
```

---

## 3. Implementation Steps

1. **Verify 7-Worker Parallel Batch**:
   - Ensure workers 0–5 on `research` and worker 6 on `command` process concurrent requests cleanly without thread collision.
   - Verify isolated Clang syntax test files (`build/temp_syntax_test_w{id}.c`).

2. **Full `.text` Range Expansion**:
   - Update `tools/auto_decomp_daemon.py` to auto-expand function scanning from `0x8C010080` all the way to `0x8C150000` whenever the pending queue dips below 50 functions.

3. **Autonomous Supervisor Watchdog (`tools/afk_supervisor.py`)**:
   - Create a resilient supervisor that loops indefinitely:
     - Runs daemon passes.
     - Automatically pushes batches to GitHub `origin/qwen`.
     - Syncs Cloudflare Pages progress every 25 matched functions.
     - Catches exit codes:
       - `0`: 100% completion (sends email alert via `ucmail`).
       - `42` (Cluster unreachable): Retries connection with 30s sleep; alerts if down > 15 minutes.
       - `43` (Storage limit): Alerts and pauses safely.

4. **Live Verification & Launch**:
   - Launch the supervisor in the background.
   - Verify several live cycles without human intervention.
   - Snapshot verification walkthrough to `reports/walkthroughs/002-2026-10-17-afk-loop-verification.md` and record in `reports/INDEX.md`.
