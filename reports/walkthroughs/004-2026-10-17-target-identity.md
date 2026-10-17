---
type: walkthrough
id: "004"
date: "2026-10-17"
title: "Target Identity Check: T1212N V1.001, not MK-51049"
status: executed
---

# Walkthrough 004: Target Identity Check

## Summary

The decomp targets the retail NTSC-U **T1212N V1.001** program. The `MK-51049 V1.007 20000217` identity used across the docs since Plan 001 was read from the IP.BIN of a DiscJuggler scene rip, and that header is not Capcom's. The descrambled `1ST_READ.BIN` agrees with the retail-targeting marvelous2 disassembly at 99.8% of 67,189 anchored points. It is **not** proven byte-identical to retail: its SHA-256 differs from marvelous2's retail hash. A Redump GDI settles that.

## Inputs

All inputs were read-only. No disc image or game state was written.

* Our image: `build-inputs/1ST_READ_descrambled.bin` (1,809,817 bytes, SHA-256 `c2470854…63e`), plus the scrambled `build-inputs/extracted/1ST_READ.BIN` as a control.
* The CDI: `E:\games\roms\dreamcast\Marvel vs. Capcom 2\Marvel vs. Capcom 2.cdi`.
* marvelous2 at `master` (fetched 2026-10-17): `build/bank01.asm`–`bank1c.asm`, `tools/extract_target.sh`, README.

## Findings

1. **The IP.BIN is a replaced header.**
   * Maker field: `SEGA ENTERPRISES`. Title: `PUT Marvel VS Capcom 2`. Product `MK-51049`, version `V1.007`, date `20000217`.
   * Capcom's licensed discs use maker `SEGA LC-T-12`. Our *Marvel vs. Capcom* CDI (`Y:\games\roms\dreamcast\Marvel vs. Capcom\`) reads `SEGA LC-T-12` / `T1202N` / `V1.000` / `19990729`.
   * `MK-` numbers are Sega first-party catalog numbers. No public listing ties `MK-51049` to this game.
2. **Retail identity.** marvelous2 states its target is NTSC-U `T1212N`, `V1.001`, with a 1,809,817-byte `1ST_READ.BIN`. That is the same size as ours.
3. **The program matches.** `tools/check_marvelous2_anchors.py` compares every `#data` word, string, pointer, `nop` and `rts` that sits directly on an address label in marvelous2's disassembly:

   | Image | Anchored matches | Mismatches |
   | :--- | ---: | ---: |
   | Descrambled (`1ST_READ_descrambled.bin`) | 67,047 | 142 |
   | Scrambled on-disc (control) | 201 | 66,988 |

   * The 142 mismatches are scattered across 17 of 28 64 KB blocks. They are densest in data (blocks 21 and 27, about 2%).
   * 18 are pointers off by 1–8 bytes, where the disassembly writes a nearby label rather than `label+offset`.
   * There is no wholesale failure of any 32-byte block, and only one mismatch falls in the last 2 KB. So the descrambler's final partial chunk is correct.
4. **Byte identity is unproven.**
   * marvelous2's `extract_target.sh` takes a plain `1ST_READ.BIN` from GD-ROM track 03 at LBA 45000 and pins SHA-256 `862e43cdd42fab395e2d71c8f1d9435f41af79380fe3b2820cb9ba25fe1f234f`.
   * Ours is `c24708540c018bc01728be117e3d405e1e91add1f0f975793eab52086298463e`.
   * Some of the 142 words may be real differences: scene patches or rip artefacts. The rest are disassembly label noise. This check cannot separate the two.
5. **Other corrections made in the same change:**
   * The loaded image ends at `0x8C1C9D99` (exclusive). The docs said `0x8C1CA499`, 0x700 bytes too far.
   * NAOMI program ROMs are `epr-23085a` and `mpr-23048`–`mpr-23084`. `epr-21576*`–`epr-21579*` are the NAOMI BIOS.
   * The PS3/360 port was by Backbone Entertainment, not Digital Eclipse.

## Changes

* `README.md`, `AGENTS.md`, `DECOMP.md`, `docs/TARGET.md` (new "Target identity" section) and `docs/MEMORY_MAP.md`: serial, version, span and NAOMI/PS3 corrections.
* `tools/inspect_dc.py`, `tools/setup_disc.py`, `tools/progress.py`: labels now say T1212N. The hash constants are unchanged; they still describe our CDI rip.
* New `tools/check_marvelous2_anchors.py`.
* Not changed: `reports/plans/001` (immutable), and the generated dashboard files, which pick up the new label on the next `progress.py` run.

## Open

* Dump or obtain the Redump GDI of *Marvel vs. Capcom 2 (USA)*. Extract `1ST_READ.BIN` from track 03 and compare it to `862e43cd…`. If it matches, switch the build target to the retail image and re-verify every matched function at the 142 addresses.
* Decide whether to coordinate with g-guthrie/marvelous2, which targets the same binary.
