---
type: walkthrough
id: "001"
date: "2026-10-16"
title: "Cluster Setup, Auto-Decompiler Daemon & Initial Subsystems Decompilation"
status: executed
---

# Cluster Setup, Auto-Decompiler Daemon & Initial Subsystems Decompilation

## 1. Summary of Execution
Successfully stood up the multi-node Apple Silicon cluster (`research` M5 Max + `command` M5 Air) connected to Windows `titan` via Tailscale. Deployed unbuffered streaming `ai-pipe`, Capstone SH-4 disassembler, real-time live preview monitor, and the autonomous queue daemon. Decompiled the initial batch of 11 functions (740 bytes) in `src/sys_init.c` and peripheral subsystems.

## 2. Verified Components
1. **Tooling & Infrastructure**:
   - `aipipe` patched and distributed across all three hosts (`titan`, `research`, `command`).
   - `tools/sh4_disasm.py`: Capstone disassembler with pool resolving.
   - `tools/decompile.py`: Real-time streaming decompiler outputting to `build/qwen_live.md`.
   - `tools/auto_decomp_daemon.py`: Queue-driven autonomous daemon with Clang compiler reviewer feedback.
2. **Decompiled Functions in `src/sys_init.c` / `src/crt0.c` / `src/main.c`**:
   - `start` (`0x8C010000`): CRT0 entry and BSS/data initialization.
   - `game_init` & `game_loop`: System startup and main game loop.
   - `dreamcast_poll_controllers` (`0x8C010080`): Maple bus 4-port controller polling.
   - `controller_update_repeat` (`0x8C0102C0`): 30-frame initial delay / 5-frame auto-repeat debounce.
   - `check_cable_type` (`0x8C010326`): Video cable type 5 (VGA box) detection.
   - `sys_reboot_to_bios` (`0x8C01033C`): Soft-reset (`ABXY+Start`) to Dreamcast BIOS via `syChainLoad`.
   - `sys_shutdown_subsystems` (`0x8C01039A`): Peripheral teardown without reboot.
   - `check_maple_disconnect_error` (`0x8C0103F8`): Maple timeout (`-23`) and disconnect (`-33`) error handler.
   - `sys_init_heap_buffer` (`0x8C010458`): 128 KB work buffer & Shinobi heap init.
   - `func_8C010412`: Dispatcher & status check with Clang-verified feedback.

## 3. Verification Evidence
- Syntax verification via `clang -fsyntax-only -I include` passed on all committed functions.
- Git checkpoint `a539a7c` committed and pushed to `origin/qwen`.
- Target repo synchronized on `research` at `/Volumes/PortableSSD/mvc2-dc`.
