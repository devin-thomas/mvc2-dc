! =====================================================================
! Dreamcast CRT0 Startup & Header Preamble
! VRAM: 0x8C010000 - 0x8C010030
! =====================================================================
    .section .crt0, "ax"
    .little
    .global start
    .global _start

start:
_start:
    nop                         ! 0x8c010000: 0x0009
    nop                         ! 0x8c010002: 0x0009
    nop                         ! 0x8c010004: 0x0009
    nop                         ! 0x8c010006: 0x0009
    nop                         ! 0x8c010008: 0x0009
    nop                         ! 0x8c01000a: 0x0009

_crt0_entry:
    .word   0xd005              ! 0x8c01000c
    .word   0x6102              ! 0x8c01000e
    .word   0xd205              ! 0x8c010010
    .word   0x2129              ! 0x8c010012
    .word   0x9204              ! 0x8c010014
    .word   0x212b              ! 0x8c010016
    .word   0x2012              ! 0x8c010018
    .word   0xd004              ! 0x8c01001a
    .word   0x402b              ! 0x8c01001c
    .word   0x0009              ! 0x8c01001e
