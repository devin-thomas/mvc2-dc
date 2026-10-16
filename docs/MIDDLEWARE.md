# Embedded SDKs & Middleware Inventory

Analysis of the linear descrambled binary confirms the following official libraries and middleware:

## 1. Sega Shinobi OS Library (v1.62)
* **Signature:** `Shinobi Library for Dreamcast Version 1.62`
* **Build Date:** `Aug 30 1999 17:32:03`
* **Subsystems Identified:**
  - `syStart` (v1.15, Nov 06 1999): Startup vector management
  - `syMalloc` (v1.50, May 31 1999): Dynamic heap allocator
  - `syCache` (v1.52, Jul 22 1999): SH-4 operand/instruction cache and store queue control
  - `syInt` (v1.50, May 31 1999): Interrupt dispatching
  - `syHw` (v1.07, Apr 26 1999): Hardware initialization
  - `syRtc` (v1.02, May 31 1999): Real-Time Clock access
  - `gdFs` (v1.08, Apr 26 1999): GD-ROM filesystem abstraction
  - `pd` / `pdKbd` / `pdVib` (v1.50 - 1.51, Jul 1999): Maple bus peripheral drivers and vibration pack control
  - `bu` (v1.47, May 25 1999): VMU Backup memory interface

## 2. Sega Kamui2 Graphics Library (v16.3.2.0)
* **Signature:** `kamui2 Ver 16,3,2,0 Build:Jul 23 1999 11:06:54`
* **Companion:** `kmdk Ver 1,3,0,0 Build:Jul 23 1999 11:03:58`
* **Purpose:** Low-level PowerVR2 3D rendering pipeline for backgrounds, 3D arenas, and special effect polygon drawing.

## 3. CRI Middleware (Audio & Video)
* **CRI ADX Audio:**
  - `ADXT Ver.5.66 Build:Aug 26 1999 12:36:27` (Streaming audio engine)
  - `ADXF Ver.5.53 Build:Jun 07 1999 11:44:16` (ADX file stream driver)
  - Character and stage tracks stored as ADX streams in `disc_fs/ADX_*.BIN`.
* **CRI Sofdec RNA Video:**
  - `mwRna Ver.1.26 Build:Aug 25 1999 21:55:36` (RNA video playback used for intro cinematics).

## 4. Dreamcast Modem & Network Stack
* **Signatures:**
  - `modem Ver 1.0306 Build:Feb 01 2000 17:00:26`
  - `mdmAPI Ver. 3.04 Build:Feb 03 2000 10:04:49`
* **Purpose:** Modem communication routines and dial-up network protocol handler (used in original network ranking and VS match modes).
