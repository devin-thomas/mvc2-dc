! Data Segment: sys_data (VRAM: 0x8c190000 - 0x8c1c9d99)
    .section .data.sys_data, "a"
    .global sys_data_data
sys_data_data:
    .incbin "assets/sys_data.bin"
