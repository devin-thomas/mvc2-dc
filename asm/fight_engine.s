! Segment: fight_engine (VRAM: 0x8c060000 - 0x8c0e0000)
    .section .text.fight_engine, "ax"
    .little
    .global fight_engine_start
fight_engine_start:
    .incbin "assets/fight_engine.bin"
