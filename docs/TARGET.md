# Target Specification: Marvel vs. Capcom 2 (Dreamcast NA)

## Overview

This repository targets the North American retail release of **Marvel vs. Capcom 2: New Age of Heroes** for the Sega Dreamcast.

## Media & Identification

| Property | Value |
| :--- | :--- |
| **Platform** | Sega Dreamcast |
| **Region** | North America (NTSC-U) |
| **Product Code** | `T1212N` (retail; see "Target identity" below) |
| **Version** | `V1.001` |
| **Release Date** | 2000-06-29 (North America) |
| **Disc Media** | GD-ROM (Single Disc, 2-Session layout) |
| **Primary Executable** | `1ST_READ.BIN` |
| **Base Load Address** | `0x8C010000` (SH-4 P1 cached RAM) |

## Binary Checksums

### On-Disc Scrambled Image (`1ST_READ.BIN`)
Extracted directly from the ISO9660 filesystem on Session 2 of the disc:
* **Size:** `1,809,817` bytes
* **SHA-1:** `0655030b9ea9ba88f94640320391163e663aff3d`
* **SHA-256:** `591c328d225f7b84eaae361730d46dbcf372e92e5b0de2ee81c674f4aa878f5f`

### Descrambled Linear RAM Executable (`1ST_READ_descrambled.bin`)
The un-permuted binary as scatter-loaded into Dreamcast main memory at boot:
* **Size:** `1,809,817` bytes
* **SHA-1:** `d792ef936edd89dfb2ce5399c6b4949460dd3d7b`
* **SHA-256:** `c24708540c018bc01728be117e3d405e1e91add1f0f975793eab52086298463e`
* **Memory Range:** `0x8C010000` - `0x8C1C9D99` (end exclusive)

### Boot Sector (`IP.BIN`)
First 16 sectors (32,768 bytes) of Track 2:
* **Size:** `32,768` bytes
* **SHA-1:** `bd2b774a6cb07211672de282ae82d7fbc242f660`
* **SHA-256:** `7f767fce2251a9a71ef3266e435a1620d04d100ec23e25c76a8e8cf561c3fb65`
* **Header String (our CDI rip, not retail):** `SEGA SEGAKATANA SEGA ENTERPRISES743C GD-ROM1/1  JUE     27BBA10 MK-51049  V1.00720000217        1ST_READ.BIN    SEGA ENTERPRISESPUT Marvel VS Capcom 2`

## Disc Layout

Dreamcast discs use a 2-session CDI/GDI format:
1. **Session 1 (Audio Track):** Lead-in and dummy/warning audio track.
2. **Session 2 (Data Track):**
   - Sectors 0-15: System security area and `IP.BIN` boot sector.
   - Sector 16: Primary Volume Descriptor (PVD, ISO9660 `CD001`).
   - ISO9660 Directory Tree: contains `1ST_READ.BIN`, character packs (`S_PL00.BIN`..`S_PL37.BIN`), stage archives (`DM00`..`DM08`), audio streams (`ADX_*.BIN`), and sound drivers (`AICADRV.BIN`).

## Target identity

The binary is the retail NTSC-U `T1212N` `V1.001` program. The `MK-51049 V1.007 20000217` header that `setup_disc.py` reports comes from the CDI rip's IP.BIN, not from the retail disc. Evidence (Walkthrough 004):

* **The IP.BIN is not Capcom's.** Its maker field is `SEGA ENTERPRISES` and its title starts with `PUT`. Capcom's licensed Dreamcast discs carry the maker code `SEGA LC-T-12` and a `T12xxN`-style product number; our *Marvel vs. Capcom* CDI reads `SEGA LC-T-12` / `T1202N`. `MK-` numbers are Sega first-party catalog numbers.
* **The program matches the retail-targeting disassembly.** [`g-guthrie/marvelous2`](https://github.com/g-guthrie/marvelous2) targets `T1212N V1.001` with a `1ST_READ.BIN` of the same size (1,809,817 bytes). Checking our descrambled image against every address-anchored value in its `build/bank*.asm` gives 67,047 matches and 142 mismatches (99.8%). The mismatches are scattered single words, mostly in data and label-arithmetic lines, with no wholesale block failure and none in the final descramble chunk. The scrambled on-disc file fails almost everywhere, as a control should. Reproduce with `tools/check_marvelous2_anchors.py`.
* **Byte identity with retail is not proven.** marvelous2's retail `1ST_READ.BIN` (plain, from GD-ROM track 03 at LBA 45000) has SHA-256 `862e43cdd42fab395e2d71c8f1d9435f41af79380fe3b2820cb9ba25fe1f234f`. Our descrambled image is `c24708540c018bc01728be117e3d405e1e91add1f0f975793eab52086298463e`. The difference is either a small scene patch or something about the rip. Settle it by extracting `1ST_READ.BIN` from a Redump GDI and comparing hashes. Until then, a function that matches our image is not guaranteed to match retail at the 142 differing words.
