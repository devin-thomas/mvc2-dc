! Segment: engine_main (VRAM: 0x8c011000 - 0x8c060000)
    .section .text.engine_main, "ax"
    .little
    .global engine_main_start
engine_main_start:
    .incbin "assets/engine_main.bin"
