! Segment: graphics_kamui (VRAM: 0x8c110000 - 0x8c150000)
    .section .text.graphics_kamui, "ax"
    .little
    .global graphics_kamui_start
graphics_kamui_start:
    .incbin "assets/graphics_kamui.bin"
