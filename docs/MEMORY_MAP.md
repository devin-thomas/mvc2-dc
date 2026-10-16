# Dreamcast Memory Map

## SH-4 Address Space Overview

The SH-4 processor maps 4 GB of virtual address space into five distinct regions based on the most significant bits:

| Region | Address Range | Cache Status | Usage |
| :--- | :--- | :--- | :--- |
| **P0 / U0** | `0x00000000 - 0x7FFFFFFF` | Cached | User area / translated space |
| **P1** | `0x80000000 - 0x9FFFFFFF` | Cached | Privileged space (Main RAM execution) |
| **P2** | `0xA0000000 - 0xBFFFFFFF` | Non-cached | Privileged space (Hardware registers, DMA) |
| **P3** | `0xC0000000 - 0xDFFFFFFF` | Cached | Privileged space (MMU-translated) |
| **P4** | `0xE0000000 - 0xFFFFFFFF` | Internal | On-chip peripheral registers, Store Queues |

## MvC2 Execution Layout

* **`0x8C000000 - 0x8C00FFFF` (64 KB):** Reserved system area / Katana bootstrap workspace.
* **`0x8C010000 - 0x8C1CA499` (1.72 MB):** `1ST_READ.BIN` linear loaded image.
  - `0x8C010000`: CRT0 entrypoint (6x `nop` slide, CRT bootstrap, Lib Handle Table).
  - `0x8C010080`: Primary initialization and engine entry.
  - `0x8C011000`: Main game loop and system dispatchers.
  - `0x8C050000`: Capcom fight engine core.
  - `0x8C0D0000`: CRI ADX sound system routines.
  - `0x8C100000`: Kamui2 PowerVR graphics pipeline.
  - `0x8C140000`: Initialized game data, animation and hitbox tables.
* **`0x8C1CA4A0 - 0x8CFFFFFF`:** Dynamic BSS variables, Shinobi heap, vertex buffers, and sound driver RAM.

## Hardware Register Bases (P2 Area)

* **`0xA05F8000`:** PowerVR2 (PVR2) 3D Graphics Core Registers
* **`0xA05F6800`:** G2 Bus Controller Registers
* **`0xA05F6C00`:** Maple Bus (Controller & VMU) Controller Registers
* **`0xA05F7000`:** GD-ROM Drive Controller Registers
* **`0xA0700000`:** AICA Sound Processor (ARM7DI + DSP) Memory Window
