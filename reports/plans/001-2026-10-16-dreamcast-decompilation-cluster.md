---
type: implementation-plan
id: "001"
date: "2026-10-16"
title: "Autonomous Multi-Host Dreamcast Decompilation Cluster"
status: executed
---

# Autonomous Multi-Host Dreamcast Decompilation Cluster

## 1. Objective
Establish an autonomous, zero-token-cost decompilation pipeline targeting the North American Sega Dreamcast executable (`1ST_READ.BIN`, Hitachi SH-4, base load address `0x8C010000`, 1.3 MB) for *Marvel vs. Capcom 2* (MK-51049).

## 2. Infrastructure & Compute Topology
- **Titan (`titan`)**: Local Windows orchestrator.
  - Hosts Git repository on branch `qwen`.
  - Runs local compiler syntax reviewer using `clang -fsyntax-only -I include`.
  - Manages background daemons, tasks, and commits.
- **Research (`research`)**: Apple M5 Max, 36GB unified memory.
  - Hosts Ollama 0.40.5 with Metal-accelerated `hf.co/unsloth/Qwen3.8-27B-GGUF:UD-Q4_K_M` (~23.4 tok/s).
  - Handles complex functions (>120 bytes) and multi-block control flows.
  - **Constraint:** Internal SSD is limited; all workspaces, tools, and build outputs are mapped to `/Volumes/PortableSSD/mvc2-dc`.
- **Command (`command`)**: Apple M5 Air, 24GB unified memory.
  - Reached via 2-hop SSH through `research` or direct Tailscale HTTP API.
  - Hosts Ollama 0.40.3 with MLX-accelerated `qwen3.5:9b` (~35.7 tok/s).
  - Handles fast, lightweight functions ($\le 120$ bytes).

## 3. Communication & Pipeline Architecture
1. **Unbuffered Streaming**: `ai-pipe` patched to stream 128-byte unbuffered chunks across Tailscale HTTP endpoints, eliminating Python socket buffer latency.
2. **Interactive Decompiler (`tools/decompile.py`)**:
   - Disassembles SH-4 opcodes with Capstone, resolving PC-relative literal pools and labels.
   - Streams reasoning thoughts and C output to `build/qwen_live.md`.
   - Uses expanded context window (`num_ctx: 16384`) to prevent Ollama HTTP 400 Bad Request errors.
3. **Autonomous Daemon (`tools/auto_decomp_daemon.py`)**:
   - Implements Builder-Reviewer loop.
   - Extracts functions sequentially or via worklist queue.
   - Feeds code to local Clang syntax check; automatically loops back compiler errors to Qwen (up to 3 retries).
   - Automatically appends valid functions to source, updates README decompilation progress badge, and commits.
   - Exits with sentinel code `42` if the cluster is unreachable or unrecoverable.
