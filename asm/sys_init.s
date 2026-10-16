! Segment: sys_init (VRAM: 0x8c010080 - 0x8c011000)
    .section .text.sys_init, "ax"
    .little
    .global sys_init_start
sys_init_start:
    .incbin "assets/sys_init.bin"
