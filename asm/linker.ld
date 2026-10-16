/* Marvel vs. Capcom 2 (Dreamcast NA) Linker Script */
OUTPUT_FORMAT("elf32-shl", "elf32-shl", "elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(start)

MEMORY
{
    ram (rwx) : ORIGIN = 0x8C010000, LENGTH = 16M
}

SECTIONS
{
    . = 0x8C010000;
    .crt0 : { *(.crt0) } > ram
    .rodata.lib_handles : { *(.rodata.lib_handles) } > ram
    .text : { *(.text*) } > ram
    .rodata : { *(.rodata*) } > ram
    .data : { *(.data*) } > ram
    .bss : { *(.bss*) *(COMMON) } > ram
}
