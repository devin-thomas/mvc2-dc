! Data Segment: lib_handles (VRAM: 0x8c010030 - 0x8c010080)
    .section .rodata.lib_handles, "a"
    .global lib_handles_data
lib_handles_data:
    .incbin "assets/lib_handles.bin"
