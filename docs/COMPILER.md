# Compiler & Toolchain Analysis

## Original Build Environment

Based on embedded string artifacts, CRT0 layout, and library signatures, the Dreamcast version of *Marvel vs. Capcom 2* was developed with Sega's official **Katana SDK** (Release 10 / 11) using the **Hitachi SuperH C/C++ Compiler (`shc.exe`)** and **Hitachi SuperH Assembler (`asmsh.exe`)**.

### Embedded Compiler Signatures
* Embedded library build dates: August 1999 – February 2000.
* Library handle entry table at `0x8C010030` (`Lib Handle Start` / `Lib Handle End`).
* Standard Katana CRT0 sequence:
  ```asm
  0x8c010000: 0x0009  ! nop (6 times)
  0x8c01000c: 0xd005  ! mov.l @(pc+0x14), r0
  0x8c01000e: 0x6102  ! mov.l @r0, r1
  0x8c010010: 0xd205  ! mov.l @(pc+0x14), r2
  ```

### Baseline Compiler Flags
For matching decompilation against Hitachi SHC:
```sh
shc.exe -cpu=sh4 -endian=little -round=nearest -pic=0 -macsave=0 -string=const -comment=nonest -sjis -O
```

For Hitachi ASMSH:
```sh
asmsh.exe -cpu=sh4 -endian=little -sjis -debug
```

### GNU Toolchain Compatibility
For modern tooling (such as `splat`, `objdump`, and decompilation testing without Wine), modern `sh-elf-gcc` (or cross-compilers targeting `sh4-linux-gnu` / `sh-elf`) can be used for disassembling, analyzing, and building relocatable object files.

### Instruction Set & Calling Conventions
* **Architecture:** Hitachi SH-4 (SH7091 core, 200 MHz, 32-bit RISC, 16-bit instructions).
* **Endianness:** Little-Endian (`elf32-shl`).
* **Registers:**
  - `r0`: Return value, temporary expression register.
  - `r1` - `r3`: Scratch / temporary registers.
  - `r4` - `r7`: Function arguments 1 through 4.
  - `r8` - `r13`: Callee-saved general registers.
  - `r14`: Frame pointer (when required) or callee-saved general register.
  - `r15`: Stack pointer (grows downward).
  - `fr0` - `fr15`: Floating-point registers (supports 32-bit single and paired 64-bit double).
  - `pr`: Procedure register (return address saved on call).
  - `gbr`: Global base register.
