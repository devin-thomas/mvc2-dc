! Data Segment: fight_tables (VRAM: 0x8c150000 - 0x8c190000)
    .section .data.fight_tables, "a"
    .global fight_tables_data
fight_tables_data:
    .incbin "assets/fight_tables.bin"
