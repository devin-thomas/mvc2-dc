# Marvel vs. Capcom 2 (Dreamcast Decompilation) - Agent Guidelines

Welcome to the Sega Dreamcast *Marvel vs. Capcom 2* (Retro REA) workspace. All agents working in this repository must adhere to the following principles:

## 1. Context & Token Preservation
* **NEVER** run recursive directory listings (`ls -R`, `Get-ChildItem -Recurse`, or broad `find`) across the workspace. With thousands of assets and disassembled binaries, broad scans pollute context windows.
* Use targeted file lookups and tool scripts (`tools/sh4_disasm.py`, `tools/decompile.py`, `tools/auto_decomp_daemon.py`).

## 2. Integrity of Source Archives & Git Branching
* The decompilation target is Sega Dreamcast (NTSC-U, serial `MK-51049`).
* Executable: `1ST_READ.BIN` (Hitachi SH-4, base load address `0x8C010000`).
* Pristine assets and disc dumps must never be corrupted or overwritten.
* Active autonomous AI-assisted decompilation takes place on the `qwen` branch.

## 3. Host & Cluster Resource Constraints
* **Research Node (`research`, Apple M5 Max, 36GB RAM)**: Internal SSD space is critically low. **Strict constraint:** All builds, checkouts, caches, and temp files must reside on `/Volumes/PortableSSD/mvc2-dc`. Never write build artifacts to `~` or `/`.
* **Command Node (`command`, Apple M5 Air, 24GB RAM)**: Internal disk space is ample. Used for lightweight functions ($\le 120$ bytes) and fast parallel passes.
* **Titan Local Node (`titan`, Windows)**: Local orchestrator, compiler syntax reviewer (`clang -fsyntax-only -I include`), Git coordinator.
* **Token Cost Rule**: Decompilation generation passes run exclusively through the local model cluster (`aipipe` / Ollama). Zero external API tokens.

## 4. Immutable Reports Ledger
All implementation plans and walkthroughs must be preserved as immutable, append-only historical records in `reports/`.
- Never edit, overwrite, or delete committed files in `reports/`.
- New revisions write a sequentially numbered snapshot (`reports/plans/` or `reports/walkthroughs/`).
- Every report must have YAML frontmatter (`type`, `id`, `date`, `title`, `status`) and be recorded in `reports/INDEX.md`.

## 5. Durable Task Dashboard
Durable multi-phase task tracking is maintained in `docs/agent-dashboard/mvc2-dc-decomp/`.
- Canonical state resides in `status.json`, with mirrored views in `STATUS.md` and self-contained `index.html`.
- Expose privately via Tailscale Serve without public exposure or Funnel.
- Checkpoint dashboard state upon phase transitions, meaningful milestones, and session handoffs.
