! Segment: sound_adx (VRAM: 0x8c0e0000 - 0x8c110000)
    .section .text.sound_adx, "ax"
    .little
    .global sound_adx_start
sound_adx_start:
    .incbin "assets/sound_adx.bin"
