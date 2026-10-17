# Marvel vs. Capcom 2 (Dreamcast NA) Decompilation Guide

This document outlines the reverse engineering methodology, architecture conventions, and matching guidelines for the Sega Dreamcast port of **Marvel vs. Capcom 2: New Age of Heroes** (NTSC-U, `T1212N` V1.001).

---

## 1. Core Principles (Retro REA Standards)

All decompilation efforts in this repository adhere to the **Retro REA** principles:

1. **Clean-Room & Ground Truth Integrity:**
   - Retail disc images belong outside the repository or in `.gitignore`-shielded local directories (`build-inputs/`).
   - Binaries are validated against cryptographic checksums (SHA-1 and SHA-256).
2. **Matching Compilation Verification:**
   - Decompiled C functions must produce byte-for-byte identical machine code when compiled with the matching toolchain.
   - Non-matching or work-in-progress routines must be clearly demarcated with `INCLUDE_ASM` stubs or `#ifndef MATCHING`.
3. **Preservation of Documented Fighter Offsets:**
   - Capcom's fighting engine maintains standard object struct offsets across CPS2, NAOMI, Dreamcast, and PS2:
     - `+0x0B`: Facing direction (`1 = right`, `-1 / 0 = left`)
     - `+0x12`, `+0x16`: World coordinates $(X, Y)$
     - `+0x1C`: Animation pointer
     - `+0x3A`: Animation frame countdown timer

---

## 2. Hardware Architecture & Toolchain

### Hitachi SH-4 (SH7091 Core)
* **Instruction Set:** SH-4 32-bit RISC, 16-bit instructions, Little-Endian (`elf32-shl`).
* **Clock Frequency:** 200 MHz.
* **FPU:** 64-bit IEEE 754 compliant (supports 32-bit single-precision, 64-bit double, 4x4 matrix transformation acceleration).
* **Virtual Addressing:** 29-bit physical addressing mapped into P0 through P4 areas.
  - Game executable loads at `0x8C010000` (P1 cached privileged memory).

### Original Compiler Baseline
* **Compiler:** Hitachi SuperH C/C++ Compiler (`shc.exe` v5.0 / v6.0) from the Sega Katana SDK (Release 10 / 11).
* **Assembler:** Hitachi SuperH Assembler (`asmsh.exe`).
* **Common Flags:**
  ```sh
  -cpu=sh4 -endian=little -round=nearest -pic=0 -macsave=0 -string=const -comment=nonest -sjis -O
  ```

---

## 3. Scrambled vs. Descrambled Executables

Dreamcast optical media uses a pseudo-random block transposition (scrambling) for executables booted from CD-R / MIL-CD formats:

* **On-Disc `1ST_READ.BIN`:** Permuted via Marcus Comstedt's PRNG slice-swap algorithm (`SHA-1: 0655030b9ea9ba88f94640320391163e663aff3d`).
* **Linear RAM Image (`1ST_READ_descrambled.bin`):** Un-permuted binary as loaded in Dreamcast RAM at `0x8C010000` (`SHA-1: d792ef936edd89dfb2ce5399c6b4949460dd3d7b`).

Decompilation and disassembly work **must always** be performed against the **descrambled linear RAM image**. Use `python tools/descramble.py descramble` or `make descramble` to unpack.

---

## 4. Decompilation Workflow

### Step 1: Stage Disc Dump
Provide your legally dumped Dreamcast CDI image:
```sh
make setup CDI="/path/to/Marvel vs. Capcom 2.cdi"
```
This automatically extracts `IP.BIN`, verifies the scrambled binary, and descrambles `1ST_READ.BIN` to `build-inputs/1ST_READ_descrambled.bin`.

### Step 2: Split Assembly & Data Segments
```sh
make split
```
This generates initial assembly stubs in `asm/` and linker script `asm/linker.ld`.

### Step 3: Decompile a Function
1. Locate the target assembly file or function offset in `asm/`.
2. Implement the corresponding C function in `src/`.
3. Check differential output using `objdiff` (`objdiff-cli` or `objdiff-gui`) or `python tools/check_match.py`.
4. Run `make progress` to update matching stats.

---

## 5. Embedded Middleware Reference

| Middleware | Version | Build Date | Header / API |
| :--- | :--- | :--- | :--- |
| **Sega Shinobi OS** | 1.62 | Aug 30, 1999 | `include/shinobi.h` |
| **Sega Kamui2 3D** | 16.3.2.0 | Jul 23, 1999 | `include/kamui2.h` |
| **CRI ADX Audio** | 5.66 / 5.53 | Aug 26, 1999 | `include/cri_adx.h` |
| **CRI Sofdec RNA** | 1.26 | Aug 25, 1999 | Video playback |
| **Katana Modem Stack** | 1.0306 | Feb 01, 2000 | Net ranking |
