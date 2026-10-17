# Marvel vs. Capcom 2 (Dreamcast NA) Decompilation

[![Tooling CI](https://github.com/devin-thomas/mvc2-dc/actions/workflows/build.yml/badge.svg)](https://github.com/devin-thomas/mvc2-dc/actions/workflows/build.yml)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Matching Progress](assets/progress.svg)](DECOMP.md)

An early matching decompilation of **Marvel vs. Capcom 2: New Age of Heroes** for the **Sega Dreamcast**, targeting the North American NTSC-U retail release (`T1212N`, `V1.001`). See [docs/TARGET.md](docs/TARGET.md) for why the `MK-51049` header on our CDI is not the retail identity.

This repository specifically focuses on the Sega Dreamcast port powered by the **Hitachi SH-4 (SH7091)** 32-bit RISC processor. Other platforms in the Marvel vs. Capcom 2 ecosystem are tracked independently:
* **PlayStation 2:** Handled separately in [`g-guthrie/mvc2-ps2-decomp`](https://github.com/g-guthrie/mvc2-ps2-decomp) (MIPS R5900 / Metrowerks CodeWarrior).
* **Arcade (NAOMI):** Same Hitachi SH-4 architecture. The game program is `epr-23085a` plus mask ROMs `mpr-23048`–`mpr-23084` (MAME set `mvsc2`); `epr-21576*`–`epr-21579*` are the NAOMI BIOS, not game code. Decompiled separately in [`g-guthrie/mvc2-naomi-decomp`](https://github.com/g-guthrie/mvc2-naomi-decomp).
* **PlayStation 3 (PSN):** Backbone Entertainment PowerPC (Cell) port, 2009, shared with the Xbox 360 release.

---

## Target

| Item | Value |
| :--- | :--- |
| **Product Serial** | `T1212N` (Version `V1.001`), retail NTSC-U. Our CDI's IP.BIN reads `MK-51049 V1.007 20000217`, a replaced scene header. |
| **Target Binary** | `1ST_READ.BIN` (Linear loaded entry `0x8C010000`) |
| **Loaded Image Span** | `0x8C010000..0x8C1C9D99` (`1,809,817` bytes, end exclusive) |
| **On-Disc Scrambled SHA-1** | `0655030b9ea9ba88f94640320391163e663aff3d` |
| **On-Disc Scrambled SHA-256** | `591c328d225f7b84eaae361730d46dbcf372e92e5b0de2ee81c674f4aa878f5f` |
| **Descrambled RAM SHA-1** | `d792ef936edd89dfb2ce5399c6b4949460dd3d7b` |
| **Descrambled RAM SHA-256** | `c24708540c018bc01728be117e3d405e1e91add1f0f975793eab52086298463e` |
| **Boot Sector (`IP.BIN`) SHA-1** | `bd2b774a6cb07211672de282ae82d7fbc242f660` |
| **Conservative Text Span** | `0x8C010080..0x8C150000` (`~1.25 MB`) |
| **Initialized Data Span** | `0x8C150000..0x8C1C9D99` (`~499 KB`) |
| **Original Compiler** | Hitachi SuperH C/C++ Compiler (`shc.exe` v5.0/v6.0) / `asmsh.exe` with `-cpu=sh4 -endian=little` |
| **Embedded SDK & Middleware** | Sega Shinobi OS v1.62, Kamui2 v16.3.2.0, CRI ADX v5.66, CRI Sofdec RNA v1.26 |

---

## Quickstart & Setup

### Prerequisites
* Python 3.10+
* Git
* GNU Make or CMake
* A legally obtained dump of your North American Dreamcast disc (`.cdi` or `.gdi` format)

### 1. Install Tooling Dependencies
```sh
pip install -r requirements.txt
```

### 2. Extract Disc Assets & Stage Executable
Point `make setup` to your retail Dreamcast CDI image:
```sh
make setup CDI="/path/to/Marvel vs. Capcom 2.cdi"
```
This performs automated validation:
* Parses DiscJuggler track layout and extracts the `IP.BIN` boot sector.
* Extracts the on-disc scrambled `1ST_READ.BIN` and validates its SHA-1 hash (`0655030b...`).
* Automatically runs the pure-Python descrambler to produce the linear loaded memory binary at `build-inputs/1ST_READ_descrambled.bin` (`d792ef93...`).

### 3. Split Binary into Assembly & Data Chunks
```sh
make split
```
Splits the binary into assembly files in `asm/` and linker script `asm/linker.ld` based on `config/mvc2_na_dc.yaml`.

### 4. Check Binary Verification & Matching Progress
```sh
make check      # Inspects staged target binary against retail fingerprints
make progress   # Generates matching code and data percentages & updates assets/progress.svg
```

---

## Repository Structure

```
mvc2-dc/
├── .github/workflows/     # CI tooling validation and progress badge automation
├── asm/                   # Disassembled assembly chunks and linker scripts
├── assets/                # Progress badges and split binary data blocks
├── config/
│   ├── mvc2_na_dc.yaml    # Binary splitting specification & section memory map
│   └── symbols.txt        # Dreamcast I/O registers and function symbol map
├── docs/
│   ├── TARGET.md          # Disc layout, product serials, and checksum specifications
│   ├── COMPILER.md        # Hitachi SHC calling conventions, flags, and literal pools
│   ├── MEMORY_MAP.md      # SH-4 virtual memory map (P0-P4) and hardware register bases
│   └── MIDDLEWARE.md      # Sega Shinobi, Kamui2, CRI ADX, and modem library inventory
├── include/
│   ├── types.h            # Standard Dreamcast integer and fixed-point typedefs
│   ├── sh4.h              # SH-4 CPU registers and hardware base addresses
│   ├── shinobi.h          # Sega Shinobi OS v1.62 API declarations
│   ├── kamui2.h           # Kamui2 3D rendering API declarations
│   ├── cri_adx.h          # CRI ADX streaming audio declarations
│   └── mvc2/
│       ├── object.h       # Capcom fighter object struct (pos, anim, box offsets)
│       ├── characters.h   # 56 character identifier enumeration
│       └── input.h        # Dreamcast controller and arcade button masks
├── src/                   # Reconstructed matching C source implementations
├── tools/
│   ├── cdi_extract.py     # Pure-Python DiscJuggler CDI parser & ISO9660 extractor
│   ├── descramble.py      # High-performance scrambler / descrambler (Comstedt algorithm)
│   ├── inspect_dc.py      # Binary inspector, middleware scanner, and hash validator
│   ├── split.py           # Config-driven SH-4 binary splitter & linker generator
│   ├── check_match.py     # Byte-for-byte differential matching validator
│   ├── progress.py        # Progress calculator and shields.io badge generator
│   └── setup_disc.py      # Automated disc extraction and setup workflow
├── objdiff.json           # Configuration for modern objdiff GUI/CLI diffing
├── Makefile               # Standard project orchestration Makefile
├── requirements.txt       # Python dependencies (pyyaml, pyelftools, tqdm)
├── DECOMP.md              # Decompilation guide, house rules, and architecture specs
└── LICENSE                # MIT License
```

---

## Modern Diffing with objdiff

This repository includes native configuration for [`objdiff`](https://github.com/encounter/objdiff):
1. Download `objdiff` GUI or install `objdiff-cli`.
2. Open the `mvc2-dc` repository folder in `objdiff`.
3. View real-time assembly side-by-side diffs with syntax coloring, register matching, and jump resolution.

---

## License

This project is licensed under the [MIT License](LICENSE). Game assets, executables, and proprietary libraries are the intellectual property of Capcom and Sega. This repository contains only reverse-engineered source code and original tooling for educational and preservation purposes.
