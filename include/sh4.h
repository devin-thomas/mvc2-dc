#ifndef SH4_H
#define SH4_H

#include "types.h"

/* Memory map segments in 29-bit SH-4 virtual space */
#define SH4_P0_BASE     0x00000000  /* User area / cached */
#define SH4_P1_BASE     0x80000000  /* Privileged cached (Game code/data) */
#define SH4_P2_BASE     0xA0000000  /* Privileged uncached (Hardware registers) */
#define SH4_P3_BASE     0xC0000000  /* Privileged translated */
#define SH4_P4_BASE     0xE0000000  /* Internal CPU registers / On-chip */

/* Dreamcast System Memory Base */
#define DC_RAM_BASE     0x8C010000
#define DC_RAM_UNCACHED 0xAC010000
#define DC_RAM_SIZE     (16 * 1024 * 1024)

/* PowerVR2 Core Registers */
#define REG_PVR_RESET        (*(volatile u32 *)0xA05F8000)
#define REG_PVR_STARTRENDER  (*(volatile u32 *)0xA05F8004)
#define REG_PVR_BORDER_COL   (*(volatile u32 *)0xA05F8040)
#define REG_PVR_FB_CFG       (*(volatile u32 *)0xA05F8044)

/* Maple Bus Controller */
#define REG_MAPLE_BASE       (*(volatile u32 *)0xA05F6C00)
#define REG_MAPLE_ENABLE     (*(volatile u32 *)0xA05F6C14)
#define REG_MAPLE_STATUS     (*(volatile u32 *)0xA05F6C18)

/* SH-4 CPU Control Registers */
#define REG_SH4_CCR          (*(volatile u32 *)0xFF000030)
#define REG_SH4_QACR0        (*(volatile u32 *)0xFF000038)
#define REG_SH4_QACR1        (*(volatile u32 *)0xFF00003C)

/* Assembly helper macros */
#define NOP()   __asm__ volatile ("nop")
#define RTS()   __asm__ volatile ("rts; nop")

#endif /* SH4_H */
