# Target Specification: Marvel vs. Capcom 2 (Dreamcast NA)

## Overview

This repository targets the North American retail release of **Marvel vs. Capcom 2: New Age of Heroes** for the Sega Dreamcast.

## Media & Identification

| Property | Value |
| :--- | :--- |
| **Platform** | Sega Dreamcast |
| **Region** | North America (NTSC-U) |
| **Product Code** | `MK-51049` |
| **Version** | `V1.007` |
| **Release Date** | `20000217` (February 17, 2000) |
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
* **Memory Range:** `0x8C010000` - `0x8C1CA499`

### Boot Sector (`IP.BIN`)
First 16 sectors (32,768 bytes) of Track 2:
* **Size:** `32,768` bytes
* **SHA-1:** `bd2b774a6cb07211672de282ae82d7fbc242f660`
* **SHA-256:** `7f767fce2251a9a71ef3266e435a1620d04d100ec23e25c76a8e8cf561c3fb65`
* **Header String:** `SEGA SEGAKATANA SEGA ENTERPRISES743C GD-ROM1/1  JUE     27BBA10 MK-51049  V1.00720000217        1ST_READ.BIN    SEGA ENTERPRISESPUT Marvel VS Capcom 2`

## Disc Layout

Dreamcast discs use a 2-session CDI/GDI format:
1. **Session 1 (Audio Track):** Lead-in and dummy/warning audio track.
2. **Session 2 (Data Track):**
   - Sectors 0-15: System security area and `IP.BIN` boot sector.
   - Sector 16: Primary Volume Descriptor (PVD, ISO9660 `CD001`).
   - ISO9660 Directory Tree: contains `1ST_READ.BIN`, character packs (`S_PL00.BIN`..`S_PL37.BIN`), stage archives (`DM00`..`DM08`), audio streams (`ADX_*.BIN`), and sound drivers (`AICADRV.BIN`).
