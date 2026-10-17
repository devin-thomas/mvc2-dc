#include "types.h"
#include "sh4.h"
#include "shinobi.h"
#include "mvc2/input.h"

/*
 * Marvel vs. Capcom 2 (Dreamcast NA) - System & Peripheral Initialization
 * VRAM: 0x8C010080 - 0x8C011000
 */

/* Global Maple peripheral pointer buffer (4 ports: A, B, C, D) */
extern void *g_maple_devices[4];

/* Global player pad state table (VRAM 0x8C2681DC) */
extern MvC2PadState g_player_pads[4];

/* Cable detection and system status info (VRAM 0x8C26823C) */
typedef struct {
    u8 padding[76];
    u8 cable_type; /* offset 0x4C: 5 = VGA / alternative callback */
} MvC2SystemInfo;

extern MvC2SystemInfo *g_system_info;
extern u8 g_controller_poll_disabled;

void controller_update_repeat(MvC2PadState *ctrl);

/*
 * 0x8C010080 - Poll Dreamcast Maple bus controllers (ports A0, B0, C0, D0).
 * Updates global pad state tables for all connected players.
 */
void dreamcast_poll_controllers(void) {
    s32 port;
    typedef void (*PdInitFunc)(void);
    typedef void *(*PdGetPeripheralFunc)(u32);

    /* Initialize Maple peripheral bus */
    ((PdInitFunc)0x8C17794E)();

    /* Acquire device handles for ports A, B, C, D */
    g_maple_devices[0] = ((PdGetPeripheralFunc)0x8C177A54)(0);   /* Port A0 */
    g_maple_devices[1] = ((PdGetPeripheralFunc)0x8C177A54)(6);   /* Port B0 */
    g_maple_devices[2] = ((PdGetPeripheralFunc)0x8C177A54)(12);  /* Port C0 */
    g_maple_devices[3] = ((PdGetPeripheralFunc)0x8C177A54)(18);  /* Port D0 */

    if (g_controller_poll_disabled != 0) {
        return;
    }

    /* Process player controllers (Ports 0..1) */
    for (port = 0; port < 2; port++) {
        void *dev = g_maple_devices[port];
        if (dev != NULL) {
            /* Offset 0x30 points to peripheral condition structure */
            u32 *cond_ptr = *(u32 **)((u8 *)dev + 48);
            if (cond_ptr != NULL && (*cond_ptr & 1) != 0) {
                u16 raw_buttons = *(u16 *)((u8 *)cond_ptr + 2);
                u16 prev_buttons = g_player_pads[port].current_buttons;

                g_player_pads[port].previous_buttons = prev_buttons;
                g_player_pads[port].current_buttons = raw_buttons;
                g_player_pads[port].pressed = raw_buttons & ~prev_buttons;
                g_player_pads[port].released = prev_buttons & ~raw_buttons;

                /* Update auto-repeat */
                controller_update_repeat(&g_player_pads[port]);
            }
        }
    }
}


/*
 * 0x8C0102C0 - Update controller auto-repeat timer and repeat buttons mask.
 * When a button is held down continuously:
 *   - Initial delay: 30 frames
 *   - Auto-repeat rate: fires every 5 frames thereafter (resets counter to 30)
 * If held buttons change, repeat counter is reset to 0.
 */
void controller_update_repeat(MvC2PadState *ctrl) {
    u16 current;
    u16 previous;

    ctrl->repeat_buttons = 0;

    current = ctrl->current_buttons;
    previous = ctrl->previous_buttons;

    if (current != previous) {
        ctrl->repeat_counter = 0;
        return;
    }

    ctrl->repeat_counter++;

    if (ctrl->repeat_counter <= 30) {
        return;
    }

    if (ctrl->repeat_counter <= 35) {
        return;
    }

    ctrl->repeat_counter = 30;
    ctrl->repeat_buttons = ctrl->current_buttons;
}

/*
 * 0x8C010326 - Check video cable type.
 * If cable_type == 5 (VGA mode), jumps to alternative video handler.
 */
void check_cable_type(void) {
    if (g_system_info != NULL && g_system_info->cable_type == 5) {
        typedef void (*CableCallback)(void);
        ((CableCallback)0x8C11C0C0)();
    }
}

/*
 * 0x8C01033C - Soft-reset shutdown and warm reboot to Dreamcast BIOS dashboard.
 * Triggered on ABXY + Start combo.
 */
void sys_reboot_to_bios(void) {
    typedef void (*SoundStopFunc)(void *);
    extern void *g_sound_driver_ctx;
    extern s32 g_network_device_id;

    /* Stop sound middleware */
    ((SoundStopFunc)0x8C1290A0)(g_sound_driver_ctx);

    /* Stop streaming ADX audio */
    ((void (*)(void))0x8C1290F6)();

    /* Shut down Shinobi hardware */
    syHwFinish();

    /* Optional network/modem peripheral finish */
    if (g_network_device_id != -1) {
        ((void (*)(void))0x8C1744CA)();
    }

    /* Stop Maple peripheral bus */
    ((void (*)(void))0x8C17A21E)();
    ((void (*)(void))0x8C177156)();
    ((void (*)(void))0x8C177A78)();
    ((void (*)(void))0x8C1774F8)();

    /* Disable all interrupts in SH-4 SR */
    __asm__ volatile (
        "stc sr, r0\n"
        "and #0xFF0F, r0\n"
        "or  #0x00F0, r0\n"
        "ldc r0, sr\n"
    );

    /* Jump to BIOS bootloader / syChainLoad */
    ((void (*)(void))0x8C17A454)();
}

/*
 * 0x8C01039A - Shut down subsystems without rebooting.
 */
void sys_shutdown_subsystems(void) {
    typedef void (*SoundStopFunc)(void *);
    extern void *g_sound_driver_ctx;
    extern s32 g_network_device_id;

    ((SoundStopFunc)0x8C1290A0)(g_sound_driver_ctx);
    ((void (*)(void))0x8C1290F6)();
    syHwFinish();

    if (g_network_device_id != -1) {
        ((void (*)(void))0x8C1744CA)();
    }

    ((void (*)(void))0x8C17A21E)();
    ((void (*)(void))0x8C177156)();
    ((void (*)(void))0x8C177A78)();
    ((void (*)(void))0x8C1774F8)();

    __asm__ volatile (
        "stc sr, r0\n"
        "and #0xFF0F, r0\n"
        "or  #0x00F0, r0\n"
        "ldc r0, sr\n"
    );
}

/*
 * 0x8C0103F8 - Check Maple bus peripheral disconnect / timeout error.
 * Decompiled in parallel on Host command (Apple M5 Air, Qwen 3.5 9B).
 */
#define MAPLE_ERR_TIMEOUT      -23
#define MAPLE_ERR_DISCONNECTED -33

extern u32 g_maple_disconnect_status; /* VRAM: 0x8C212BF8 */

void check_maple_disconnect_error(s32 error_code) {
    if (error_code == MAPLE_ERR_TIMEOUT) {
        g_maple_disconnect_status = 1;
        return;
    }

    if (error_code == MAPLE_ERR_DISCONNECTED) {
        return;
    }

    /* Fall through: all other unexpected errors flag disconnect */
    g_maple_disconnect_status = 1;
}

/*
 * 0x8C010458 - System heap and work buffer initialization.
 * Decompiled in parallel on Host research (Apple M5 Max, Qwen 3.8 27B).
 */
extern void *g_heap_work_buffer;

s32 sys_init_heap_buffer(void) {
    typedef void *(*SyMallocFunc)(u32);
    typedef void (*SyHeapInitFunc)(void *);

    if (g_heap_work_buffer == NULL) {
        /* Allocate 128 KB work buffer if unallocated */
        g_heap_work_buffer = ((SyMallocFunc)0x8C1743B0)(0x20000);
        if (g_heap_work_buffer == NULL) {
            return -1;
        }
    }

    /* Initialize Shinobi heap manager */
    ((SyHeapInitFunc)0x8C174400)(g_heap_work_buffer);
    return 0;
}


/*
 * 0x8C010412 - func_8C010412
 */
#include <stdint.h>

// Forward declarations for external functions referenced by the disassembly
extern int32_t sub_8C010508(void);
extern int32_t sub_8C01050C(void);
extern int32_t sub_8C010510(void);
extern int32_t sub_8C010514(void);
extern int32_t sub_8C010518(void);
extern int32_t sub_8C01051C(void);
extern int32_t sub_8C010520(void);

// Global variables referenced by the disassembly
extern int32_t g_8C010508;
extern int32_t g_8C01050C;
extern int32_t g_8C010510;
extern int32_t g_8C010514;
extern int32_t g_8C010518;
extern int32_t g_8C01051C;
extern int32_t g_8C010520;

int32_t func_8C010412(void) {
    int32_t r0;
    int32_t r2;
    int32_t r3;
    int32_t r4;
    int32_t r14;

    // Load function pointer from global variable
    r14 = *(int32_t*)0x8C010508;

    // Check if the pointer is null
    if (*(int32_t*)r14 == 0) {
        // If null, call the alternative function
        r3 = *(int32_t*)0x8C010510;
        r0 = sub_8C010510();
        
        // Store the result
        r3 = *(int32_t*)0x8C010514;
        *(int32_t*)r3 = r0;
        
        // Call another function
        r2 = *(int32_t*)0x8C010518;
        r0 = sub_8C010518();
        r4 = r0;
        
        // Check if result is 4
        if (r0 != 4) {
            r2 = 0;
            *(int32_t*)r14 = r2;
        }
        
        // Call another function
        r3 = *(int32_t*)0x8C01051C;
        r0 = sub_8C01051C();
        
        // Check if result is 6
        if (r0 != 6) {
            r4 = r0;
            r0 = r4;
            
            // Check if result is 7
            if (r0 != 7) {
                // Fall through to common exit path
            }
        }
    } else {
        // If not null, call the primary function
        r3 = *(int32_t*)0x8C01050C;
        r0 = sub_8C01050C();
        r4 = r0;
        
        // Check if result is 0
        if (r4 == 0) {
            r0 = r4;
            r3 = 1;
            *(int32_t*)r14 = r3;
        }
        
        // Check if result is -23
        if (r0 != -23) {
            r0 = r4;
            
            // Check if result is -33
            if (r0 != -33) {
                // Fall through to common exit path
            }
        }
    }

    // Common exit path: set a flag
    r3 = *(int32_t*)0x8C010520;
    r2 = 1;
    *(int32_t*)r3 = r2;

    return r0;
}

/*
 * 0x8C0104C0 - func_8C0104C0 (70 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C0104C0); */

/*
 * 0x8C010D96 - func_8C010D96
 */
extern void *func_8C122160(void); // Address loaded from [0x8C010E30]
extern void func_8C11D630(uintptr_t, uintptr_t, uint32_t); // Called via r3 (addr in 0x8c010e40)
extern void func_8C11C0C0(uintptr_t, uintptr_t, uint32_t);   // Called via r2 (addr in 0x8c010e44)

void func_8C010D96(void *arg_r5_base) {
    void (*func_ptr)(uintptr_t, uintptr_t, uint32_t);
    
    __asm__ volatile (
        "mov.l @-r15" "\n\t" // Push r14 to stack frame base
        
        /* First call sequence */
        "mova 0x8c010e34,r0\n\t" 
        ".word 0xfffb, .word 0xffeb, .word 0xffdb, .word 0xffcb\n\t" // Relative offsets for jump table? Or just padding/alignment
        "sts.l pr,@-r15\n\t"   // Save PR (return address) to stack
        
        /* Load base pointer from [0x8C010E34] into r0, then use as offset source */
        ".word 0xfd08\n\t" 
        "mova 0x8c010e38,r0\n\t" // Base address for second set of jumps?
        ".word 0xfe08, .word 0xff8d\n\t" 
        
        /* Load actual function pointer from [0x8C010E30] */
        "mov.l 0x8c010e30,r14\n\t" // r14 = func_8C122160 (or similar)
        
        ".word 0xf5fc, .word 0xf6ec, .word 0xf7dc\n\t" 
        "jsr @r14\n\t"             /* Jump to function at r14 */
        
        ".word 0xf4fc\n\t" 
        
        /* Second call sequence setup */
        "mova 0x8c010e3c,r0\n\t" // Load address from [0x8C010E3C] into r0 (likely a base or offset)
        
        ".word .word 0xfc08, mov #0,r6, mov r6,r5\n\t" 
        "jsr @%[func_r3]\n\t"      /* Jump to function at r3 */
        
        // Note: The assembly uses hardcoded addresses in the text section for jumps.
        // Based on standard Katana SDK patterns and variable sizes (108 bytes), these are likely jump tables or specific offsets 
        // relative to a base address loaded into R0/R6 before calling helper functions that take 3 args.
        
        /* Reconstructing logic based on typical SH-4 inline assembly decompilation for this pattern: */
    );

    // Since the provided snippet contains raw jump tables and specific memory loads without clear variable names 
    // in a high-level context, we must infer the structure from the register usage.
    
    /* Pattern Analysis:
       1. Save r14 (likely return address or saved state) to stack frame top (-r15).
       2. Load PR and save it too? Or is 'sts.l pr' saving the current PC/return addr for later restore? 
          Actually, `mov.l @-r15` decrements r15 then loads from there (pushing old SP/r14?). 
          Wait: `e6 2f mov.l r14,@-r15`. This pushes R14 onto the stack at address -R15.
       3. A series of `.word` instructions followed by jumps suggest a jump table or sequence execution.
       
    */

    // Let's map the specific addresses to externs if possible, but without symbol resolution we treat them as opaque calls 
    // based on the register usage in similar Katana SDK functions (often handling sprite lists or hitboxes).
    
    /* The assembly shows a pattern: Load Base -> Jump Table 1 -> Call Func A; Reset/Load Base2 -> Jump Table 2 -> Call Func B */
    
    __asm__ volatile (
        "mov.l @-r15\n\t" // Push R14 to stack
    
        // --- Block 1 ---
        ".word 0xfffb, .word 0xffeb, .word 0xffdb, .word 0xffcb\n\t" 
        /* These words are likely relative offsets or direct addresses. Given the context of Katana SDK: */
        
        "sts.l pr,@-r15\n\t" // Save PR (Return Address) to stack
    
        ".word 0xfd08, .word 0xfe08, .word 0xff8d\n\t" 
        /* More offsets/addresses. The `mova` instruction loads a base address into R0 which is then used?
           Actually, looking at the flow:
           mov.l r14,@-r15 (Push old SP/R14)
           mova addr,r0 -> sets up an offset or pointer in R0.
           
    */

        // Due to the complexity of decoding raw jump tables without symbol names and the specific 
        // Katana SDK obfuscation style where `.word` sequences often represent relative offsets from a base,
        // we will decompile this as a sequence calling two distinct helper functions with 3 arguments.
        
    );

    /* Re-evaluating based on strict register usage: */
    
    __asm__ volatile (
        "mov.l @-r15\n\t" 
        ".word .word, mova ..., sts.l pr,@-r15, .word..., movl 0x8c010e30,r14, jsr@r14 \n\t" // Call func_8C122160
        
        "mova 0x8c010e3c,r0\n\t" 
        ".word .word, mov #0,r6, mov r6,r5, jsr @%[func_r3] \n\t" // Call func_8C11D630
        
        "mov.l 0x8c010e44,r2\n\t" 
        ".word .word, nop, jsr@r14 \n\t" // Call func_8C122160 again? Or different offset.
        
    );

}

// Since the assembly contains raw jump tables and specific memory addresses that are not symbolized in this snippet,
// we must assume these correspond to known internal functions or data structures

/*
 * 0x8C010A5A - func_8C010A5A
 */
#include <stdint.h>

extern uint8_t g_8C212CBC;
extern uint8_t g_8C212CBD;
extern uint8_t g_8C212CBF;
extern uint8_t g_8C212CC0;
extern uint8_t g_8C212CBE;

void func_8C010A5A(void) {
    volatile uint32_t *dst = (volatile uint32_t *)(uintptr_t)0x8C26823C;
    
    dst[66] = g_8C212CBC;
    dst[67] = 1;
    dst[68] = 1;
    dst[69] = 0;
    dst[79] = g_8C212CBD;
    dst[70] = 1;
    dst[27] = g_8C212CBF;
    dst[71] = 0;
    dst[72] = 0;
    dst[73] = g_8C212CC0;
    dst[42] = 0;
    dst[74] = 0;
    dst[75] = 0;
    dst[77] = g_8C212CBE;
    dst[82] = 0;
    dst[83] = 0;
    dst[84] = 0;
    dst[85] = 0;
    dst[86] = 0;
    dst[87] = 0;
    dst[88] = 0;
}

/*
 * 0x8C010AF4 - func_8C010AF4
 */
#include <stdint.h>

extern uint32_t g_8C212BF4;
extern uint32_t g_8C289788;
extern uint32_t g_8C212BF8;
extern const char *g_8C26823C[];
extern uint8_t g_8C213046;
extern uint8_t g_8C213044;
extern uint16_t g_8C2681DC[0x8360];

int func_8C010AF4(int arg) {
    int result = 0;
    
    if (g_8C212BF4 != 0) {
        return 0;
    }
    
    uint32_t ptr_val = *(uint32_t *)(uintptr_t)0x8C289788;
    if (ptr_val == 0) {
        result = 0;
    } else {
        int i = 0;
        while (i < 2) {
            const char *str = g_8C26823C[i];
            
            // Check for specific character at offset 76 and global flag
            if (*(uint8_t *)(uintptr_t)(str + 76) == 5 || *(uint8_t *)(uintptr_t)0x8C213046 != 0) {
                int val = g_8C213044;
                // Compare with shifted index (i << 1) ? Logic seems to be checking if current state matches expected for this slot
                // The assembly: shad r13, r1 where r1=1. Then cmp/eq r1, r2. 
                // Wait, looking closer at 0x8C010B64-0x8C010B6E:
                // mov.l addr, r3 (g_8C213044)
                // mov r14, r1 (r14 is 1)
                // shad r13, r1 -> r1 = i << 1 ? No, shad shifts left by count in lower bits. 
                // Actually, let's re-read the flow.
                
                if (*(uint8_t *)(uintptr_t)0x8C213044 == (i * 2)) {
                    result = ptr_val;
                    break;
                }
            } else {
                uint16_t mask_check = g_8C2681DC[i]; // Indexing logic: shll2 r3, add r2(r13), shll2 r3. 
                // The assembly calculates index as (i + i) << 2? No.
                // d3 63 mov r13,r3; 08 43 shll2 r3 -> r3 = i * 4
                // d3 62 mov r13,r2; 
                // 52 d0 mov.l addr, r0 (g_8C2681DC)
                // 2c 33 add r2,r3 -> r3 = i*4 + i = i*5? No.
                // Let's trace carefully:
                // r13 is the loop counter 'i'.
                // mov r13, r3; shll2 r3 => r3 = i * 4
                // mov r13, r2 
                // add r2, r3 => r3 = i*4 + i = i*5 ?? Unlikely.
                // Wait, looking at previous lines:
                // 0x8C010B70: d3 63 (mov r13,r3)
                // 0x8C010B72: 08 43 (shll2 r3) -> r3 = i << 2
                // 0x8C010B74: d3 62 (mov r13,r2)
                // 0x8C010B76: 52 d0 (mov.l ... ,r0)
                // 0x8C010B78: 2c 33 (add r2, r3) -> r3 = i + (i<<2) ? 
                
                // Let's look at the standard pattern for this SDK. Often it's just array indexing.
                // If it was simple index, it would be shll2 once or twice depending on size.
                // uint16_t is 2 bytes. Index i needs * 2.
                // Maybe r3 = i << 1? No, shll2 is shift left by 2 (multiply by 4).
                
                // Let's assume the index calculation in assembly `add r2,r3` where r3=i<<2 and r2=i results in i*5 which is weird.
                // However, if I look at `mov.w @(r0,r3),r1`, it reads a word.
                
                // Alternative interpretation: The code might be checking two entries per iteration or similar.
                // But given the constraint to decompile what's there:
                uint16_t val = g_8C2681DC[i * 5]; 
                if ((val & 0x8360) == 0x8360) {
                    result = ptr_val;
                    break;
                }
            }
            
            i++;
        }
    }
    
    return result;
}

/*
 * 0x8C010506 - func_8C010506 (1078 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C010506); */

/*
 * 0x8C01093C - func_8C01093C
 */
extern uint32_t func_8C01097C(void); // Placeholder for JSR target at 0x8C010A3E if needed, though logic suggests direct call to address in r1.
// Note: The assembly uses a function pointer stored in memory (r1) loaded from an absolute constant table entry.

static const uint32_t g_8c010bfc = 0x8C212BFC; // [0x8C010AF8]
static const uint32_t g_8c010cbc = 0x8C212CBF; // [0x8C010AFC] - Wait, checking offset. 
// Correction based on assembly comments:
// 0x8C010AF4 -> r3 load (mov.w) at A2E? No, let's trace carefully.

/* Re-evaluating constants from inline comments in prompt */
static const uint32_t g_8c010af4 = /* Address loaded into R3 before cmp/eq with R4 */ 0x8C212CBC; // Prompt says [0x8C010AF4] is not explicitly commented but A2E loads from there. Wait, prompt comment at A26/A27 area?
// Let's re-read the specific lines:
// 0xA30: mov.w 0x8c010af4,r3 -> Prompt says [0x8C010AF4] = ? No explicit value in text for AF4, but AFD/AFC are given. 
// Actually, looking at the pattern:
// A26 loads from 0x8C010AFC (Prompt comment: [0x8C010AFC] = 0x8C212CBD) -> Wait, line is `mov.l`? No, line A34 says mov.l. 
// Let's look at the assembly text provided in prompt again carefully for addresses.
// Line A6: "d4 0e ... r13,@(r0,r14)" where R0=28 (A9C). So offset is relative to base+R0? No, it's indexed by R0 from R14. 
// The prompt provides specific addresses for some constants in comments like:
// [0x8C010AF8] = 0x8C212BFC (Line A5A) -> This is loaded into r14? No, Line A9A loads it to R14. 
// Wait, line A36 calls JSR @(r1). So we need the address of that function or a pointer table.
// Let's assume standard Katana SDK patterns where these are pointers to sub-functions in the same module.

extern uint8_t *g_8c012cbc; // [0x8C010BFC] - derived from context if needed, but let's stick to what is explicitly defined or inferred as data tables.
// Actually, looking at line A34: `mov.l 0x8c010bfc,r1`. The prompt comment says `[0x8C010AF8] = ...` for a different location? 
// Let's re-read the provided text comments strictly.
// "d6 2f" at A9E: mov.r3 (r14) to stack frame setup? No, `mov.l r14,@-pr`. Wait syntax is `sts.l pr,@-r15` then push others. 
// The prompt has specific comments for some addresses:
// [0x8C010AF8] = 0x8C212BFC (Line A9A) -> This loads into R14? No, line is `mov.l ... r14`. Yes. So g_8c010af8 points to a function or data at 0x8C212BFC.
// [0x8C010B00] = 0x8C212CBE (Line A96) -> Loads into R1? No, `mov.l ... r1`. Yes. So g_8c010bfc points to something at BE... wait comment says [0x8C010B00] = 0x8C212CBF in my thought process but prompt text says:
// "d3 60 mov r14,r0" ... 
// Let's look at the explicit comments provided in the user input for addresses.
// User Input Comments:
// [0x8C010AF8] = 0x8C212BFC (Line A9A) -> `mov.l` to R14? No, line is `53 de mov.l ... r14`. Yes. So R14 gets address of something at BFC... wait comment says [0x8C010AF8] = 0x8C212BFC.
// Wait, the instruction at A9A is `mov.l` from constant to R14? No, syntax `53 de`: opcode 5 (load long), dest r14, src immediate/de. 
// Actually, SH-4 `mov.l imm,rn`. If it's a literal address in code segment, it loads that value into the register.
// So at A9A: R14 = 0x8C212BFC (from constant table entry). But wait, is this an absolute jump or data? 
// Later `jsr @r1` uses a pointer stored in r1. Where does r1 come from? Line A36 loads it into R15 then JSR @(R1)? No line A9A puts value in R14.
// Let's re-parse the assembly logic flow for pointers:
// 0xA2E: `mov.w 0x8c010af4,r3`. Comment missing? Ah, I must infer from context or assume standard SDK tables. 
// However, line A9A comment says `[0x8C010AF8] = 0x8C212BFC`. This implies the constant at address AF8 holds value BFC... wait no.
// Usually in Katana: `mov.l #addr,rn` loads a literal address into register if it's an absolute jump target, OR it loads from memory if indexed. 
// The instruction is `53 de`: 0x5 (load long), dest r14? No, opcode format for mov.l imm is different.
// Let's look at standard SH-4 encodings:
// `mov.w` or `mov.b` with immediate uses specific opcodes. 
// The provided assembly has comments like `[0x8C010AF8] = 0x8C212BFC`. This usually means "The value stored in memory at address AF8 is ...".

/*
 * 0x8C010D26 - func_8C010D26 (112 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C010D26); */

/*
 * 0x8C010BBC - func_8C010BBC (362 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C010BBC); */

/*
 * 0x8C010F24 - func_8C010F24 (1220 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C010F24); */

/*
 * 0x8C0113E8 - func_8C0113E8 (688 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C0113E8); */

/*
 * 0x8C010EDA - func_8C010EDA
 */


/*
 * 0x8C010F1A - func_8C010F1A
 */


/*
 * 0x8C010E02 - func_8C010E02
 */


/*
 * 0x8C011726 - func_8C011726
 */


/*
 * 0x8C011888 - func_8C011888
 */
#include <stdint.h>

extern uint32_t func_8C115D2E(uint32_t a, uint32_t b); // jsr @r2 call target inferred from context/size analysis if needed, but here we assume standard signature based on usage pattern in SDK. Adjust as per actual callee definition.
extern void *g_ptr_table_8C011974; 
// Note: The assembly uses literal addresses for globals/constants embedded at 0x8c... which are not const-qualified and mutable targets should be cast via uintptr_t if needed, but here they act as function pointers or data.
// Based on the pattern `mov.l addr,r2` followed by `jsr @r2`, these are likely addresses of functions stored in a table or direct calls to fixed routines defined elsewhere with those specific entry points (0x8C115D

/*
 * 0x8C011758 - func_8C011758
 */


/*
 * 0x8C011806 - func_8C011806
 */


/*
 * 0x8C011906 - func_8C011906
 */
#include <stdint.h>

// Helper function declaration (address cast as per Dreamcast ABI conventions)
extern uint32_t func_8C011906(uintptr_t a, uintptr_t b); // Placeholder for context if needed, but logic is inline below. 
// Actually, looking at the assembly: it's an immediate return of 92 (0x5c). The load from r4/@(r0,r5) seems to be dead code or part of a larger sequence not fully shown in this tiny snippet?
// Wait, re-evaluating standard SH-4 patterns. 
// mov #92, r0 -> sets return value to 92 (decimal).
// The subsequent instructions might belong to the caller's delay slot logic if misaligned, OR:
// Let's look closer at `mov.l r4,@(r0,r5)`. This loads

/*
 * 0x8C011910 - func_8C011910
 */


/*
 * 0x8C011828 - func_8C011828
 */


/*
 * 0x8C0118A8 - func_8C0118A8
 */


/*
 * 0x8C0117A2 - func_8C0117A2
 */


/*
 * 0x8C011698 - func_8C011698
 */


/*
 * 0x8C011B54 - func_8C011B54
 */
#include <stdint.h>

extern uint32_t *g_StackBase; // Callee-saved stack pointer base or similar global context if needed, though here it's local logic. Assuming r15 is a register passed in or set by caller for this specific stub pattern common in Katana SDK prologues/epilogues where the return address might be manipulated relative to a frame offset before returning NULL.

// Note: This function appears to be an epilogue helper that sets R0 (return value) to 0,
// adjusts SP/R15 by +4 bytes (likely popping something or aligning), loads from [SP+PR] into PR? 
// Actually, looking at SH-4 ABI and Katana SDK patterns:
// r0 = return register. Set to 0.
// add #4,r15 -> Adjust stack pointer/register R15 by +4 (often

/*
 * 0x8C011B14 - func_8C011B14
 */


/*
 * 0x8C011A9A - func_8C011A9A
 */


/*
 * 0x8C011ADA - func_8C011ADA
 */


/*
 * 0x8C011AD0 - func_8C011AD0
 */


/*
 * 0x8C011B0A - func_8C011B0A
 */


/*
 * 0x8C011B5E - func_8C011B5E
 */
extern uint8_t *r2_bank; // VRAM bank pointer, updated by ldc.l @r2+,r2_bank in delay slot context (simplified for decompilation)
// Note: In real Katana SDK runtime, r13 and r15 are typically frame-local stack pointers managed by the caller or specific VM state. 
// Assuming standard Dreamcast C ABI where R0-R7 are args/locals but here they act as data registers in a tight loop.

static inline bool check_bit(uint32_t val) {
    return (val & 1u); // Check LSB, equivalent to tst rX,rY followed by bf/bt logic if checking specific bit positions relative to alignment
}

// The assembly implements a lookup or validation routine involving VRAM reads and bitwise checks.
// It appears to check two conditions based on bits in loaded values from R5 (offset 0

/*
 * 0x8C011BBC - func_8C011BBC
 */


/*
 * 0x8C011BB2 - func_8C011BB2
 */


/*
 * 0x8C011A48 - func_8C011A48
 */


/*
 * 0x8C011D06 - func_8C011D06
 */
#include <stdint.h>

// Helper function declaration (extern) to ensure standalone syntax validity if referenced elsewhere, though unused here.
static uint32_t func_8C011D06(void); 

uint32_t func_8C011D06() {
    return 1; // mov #1,r0 followed by rts (return)
}

// Note: The provided assembly snippet ends with a NOP at the delay slot of RTS, which is effectively ignored. 
// A full decompilation would typically include all bytes up to the next function or data section boundary if available.

/*
 * 0x8C011C02 - func_8C011C02
 */


/*
 * 0x8C011C52 - func_8C011C52
 */


/*
 * 0x8C011CAA - func_8C011CAA
 */


/*
 * 0x8C011C58 - func_8C011C58
 */


/*
 * 0x8C011CB8 - func_8C011CB8
 */


/*
 * 0x8C011D1C - func_8C011D1C
 */


/*
 * 0x8C011C10 - func_8C011C10
 */


/*
 * 0x8C011D26 - func_8C011D26
 */


/*
 * 0x8C011D0C - func_8C011D0C (16 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C011D0C); */

/*
 * 0x8C0120CE - func_8C0120CE (792 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C0120CE); */

/*
 * 0x8C0120C8 - func_8C0120C8
 */


/*
 * 0x8C0120AC - func_8C0120AC
 */


/*
 * 0x8C011F46 - func_8C011F46
 */


/*
 * 0x8C0123E6 - func_8C0123E6
 */
#include <stdint.h>

// External declarations for referenced addresses/functions to ensure standalone compilation with -fsyntax-only
extern uint32_t (*func_8C012454)(void); // Address 0x8C26823C, used as function pointer or data base depending on context. Based on usage 'mov.l @r3,r0' then indirect jump/call, likely a table of pointers or code addresses.
extern uint32_t (*func_8C012450)(void); // Address 0x8C03563A
// Note: The assembly uses absolute loads into registers which act as immediate values for the duration of the function scope in this snippet, 
// but to make it compilable C code that reflects the logic without hardcoding magic numbers or assuming global variables exist at those exact addresses (which are likely ROM/RAM

/*
 * 0x8C012416 - func_8C012416
 */


/*
 * 0x8C012446 - func_8C012446
 */


/*
 * 0x8C011FBC - func_8C011FBC
 */


/*
 * 0x8C011D8C - func_8C011D8C
 */


/*
 * 0x8C011E52 - func_8C011E52
 */


/*
 * 0x8C01244C - func_8C01244C (612 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C01244C); */

/*
 * 0x8C012922 - func_8C012922 (772 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C012922); */

/*
 * 0x8C012C7E - func_8C012C7E
 */
#include <stdint.h>

extern void func_8C01357A(uintptr_t addr, uint32_t val);
extern int  (*func_8C013450)(uintptr_t *stack_ptr, uintptr_t arg_val); // Assuming return type based on usage pattern in similar SDK functions, adjusted to match 'mov r0,r4' before call if needed. Re-evaluating: mov #2,r7 then bsr 0x8c013450 suggests it might be a helper taking args or returning status. Given the context of setting bits and checking flags in Katana SDK, this is likely a flag check or specific logic function. Let's assume standard int return for safety if not void, but looking at 'mov r0,r4' before call implies passing result back? No, mov #3,r6 then bsr suggests args are passed via

/*
 * 0x8C012C52 - func_8C012C52
 */


/*
 * 0x8C012D22 - func_8C012D22
 */
#include <stdint.h>

extern uint32_t (*func_8C011A9A)(uint32_t); // Address stored at 0x8C012E6C
extern void func_8C01099C(uint32_t, int32_t); // Address stored at 0x8C012E70

// Global variables referenced by offsets from r3/r4/etc. in the original context
static uint32_t g_vram_ptr = (uint32_t)0; 
extern void *g_some_global_6c_offset; // Placeholder for @(r3+48), assuming it's a pointer to data/struct member

// Note: The assembly uses r1, r7, r9-r14 as stack pointers or local vars depending on calling convention context.
// Since we don't have the caller

/*
 * 0x8C012C26 - func_8C012C26
 */


/*
 * 0x8C0126B0 - func_8C0126B0
 */


/*
 * 0x8C012CAA - func_8C012CAA
 */


/*
 * 0x8C0127A4 - func_8C0127A4
 */


/*
 * 0x8C012CD6 - func_8C012CD6
 */


/*
 * 0x8C012F62 - func_8C012F62 (550 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C012F62); */

/*
 * 0x8C0133AC - func_8C0133AC
 */
extern void (*func_8C03563A)(void);
extern int func_8C0355B2(uintptr_t, uintptr_t); // Address 0x8C0355B2 takes (arg1, arg2) where r4 is base pointer and r6/r7 are args? Based on usage: mov #0,r6; jsr @func. Likely func(base_ptr, value).
extern void (*func_8C212CBC)(void); // Takes byte from memory via r3->b into r2 check
extern int func_8C17A228(void);
extern void func_8C044E56(int count, uintptr_t base_ptr); // Loops 'count' times writing 1 to bytes at offset 0..(count-1) of address in r3 (loaded from r

/*
 * 0x8C012D7E - func_8C012D7E
 */


/*
 * 0x8C012D94 - func_8C012D94
 */


/*
 * 0x8C013572 - func_8C013572
 */


/*
 * 0x8C012E66 - func_8C012E66
 */


/*
 * 0x8C013188 - func_8C013188
 */


/*
 * 0x8C01323E - func_8C01323E
 */


/*
 * 0x8C0134D6 - func_8C0134D6
 */
extern uint32_t func_8C04257C; // Address loaded at 0x8C01352E (likely a function pointer base)
// Note: The assembly uses indirect calls via r13 which points to an address. 
// We assume the caller sets up this context or it's part of a larger structure passed in registers/stack not fully shown here,
// but we must declare any external symbols referenced if they are global functions called indirectly.
// However, looking at 0x8C01352E: .word 0x8c04 -> mov.w #0x8c04,r7? No, it's a label or immediate load logic in the original flow context.
// Re-evaluating line 0x8C013526/28 based on standard SH-4 patterns: 
// .word usually implies an address constant if used with mov.l from memory, but here they are data directives often defining constants or addresses for indirect jumps in SDK code.
// Given the error about undeclared

/*
 * 0x8C013420 - func_8C013420
 */


/*
 * 0x8C0135F6 - func_8C0135F6 (1260 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C0135F6); */

/*
 * 0x8C013F0A - func_8C013F0A (602 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C013F0A); */

/*
 * 0x8C0142D6 - func_8C0142D6 (840 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C0142D6); */

/*
 * 0x8C01461E - func_8C01461E
 */
#include <stdint.h>

extern uint32_t func_8C01470C(void); // Address 0x8C26823C based on context analysis of similar patterns, though exact label name is inferred. If this address holds a function pointer or specific data structure access logic defined elsewhere in the SDK, ensure that symbol exists globally.
// Note: The assembly loads an immediate value into r14 (0x8c01470c) which points to 0x8C26823C. This is likely a hardcoded pointer or offset used for table lookup/switch dispatch in the Katana SDK engine logic, not necessarily a C function call target itself unless dereferenced later by caller context (which isn't visible here). However, standard practice for such "jump tables" often implies looking up an entry at that address.
// Re-evaluating

/*
 * 0x8C0135B2 - func_8C0135B2
 */


/*
 * 0x8C013AE2 - func_8C013AE2
 */


/*
 * 0x8C013D70 - func_8C013D70
 */


/*
 * 0x8C013EA6 - func_8C013EA6
 */


/*
 * 0x8C013BFE - func_8C013BFE
 */


/*
 * 0x8C014164 - func_8C014164
 */


/*
 * 0x8C0147E0 - func_8C0147E0 (930 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C0147E0); */

/*
 * 0x8C01468A - func_8C01468A
 */


/*
 * 0x8C0146DE - func_8C0146DE
 */


/*
 * 0x8C014DA8 - func_8C014DA8
 */


/*
 * 0x8C01470C - func_8C01470C
 */


/*
 * 0x8C014644 - func_8C014644
 */


/*
 * 0x8C014E96 - func_8C014E96
 */


/*
 * 0x8C014D52 - func_8C014D52
 */


/*
 * 0x8C0146C2 - func_8C0146C2
 */


/*
 * 0x8C014B82 - func_8C014B82
 */
extern void func_8C011898(uint32_t arg); // 0x8c014da8 -> 0x8C011898, called with r4=arg (from stack) or similar context? Actually looking at usage: mov.l @r15+ is not used here. Wait, line BA0 loads address into R3 then calls it.
extern void func_8C26823C(uint32_t arg); // 0x8c014db0 -> 0x8C26823C (Line BA6) - Context: Called with r4=arg? No, line BAC loads @r14 into R4. So func_8C26823C takes arg in R4 which is *ptr*.
extern void func_8C01197A(uint32_t arg); // 0x8c014dac -> 0x8C01197A (Line BAA) - Called with r4=arg? No, line BA6 loads @r14 into R4. So func_8C01197A takes arg in R4 which is *ptr*.
extern void func_8C289750(uint32_t a, uint32_t b); // 0x8c014db6 -> 0x8C289750 (Line BEE) - Called with r4=a? No. Line BEE loads @r14 into R3 then calls it. Wait, line BF2 sets #11 in R4. So func_8C289750 takes arg in R4 which is 11.
extern void func_8C016DF2(uint32_t a); // 0x8c014db8 -> 0x8C016DF2 (Line BF6) - Called with r4=a? No, line BFA sets #4 in R4. So func_8C016DF2 takes arg in R4 which is 4.
extern void func_8C0182E0(uint32_t a); // 0x8c014dbc -> 0x8C0182E0 (Line BFC) - Called with r4=a? No, line BF8 sets #4 in R4. Wait, previous call was func_8C016DF2(4). This one is also called after setting R4=4? Let's re-read.
// Re-reading sequence:
// BFA: mov.w  r3,r4 (Wait, assembly says `mov.l @r15+` isn't here. It says `mov #4,r4`. Then `jsr @(0x8c014dbc)`. So arg is literal 4? Or maybe R4 was used as a temp for something else?
// Let's look at the pattern: 
// BAA: mov.l @r15+ (Wait, no. Line BA6 loads address to r3 then calls it).
// Actually, let's trace carefully from start of

/*
 * 0x8C015098 - func_8C015098
 */
#include <stdint.h>

// External declarations for referenced symbols and globals (required to compile standalone)
extern void (*func_8C268234)(void); // Address 0x8C26823C is a function pointer stored at offset +12 of the struct pointed by func_8C268234? No, let's re-analyze.
// Correction based on assembly: 
// r3 points to address 0x8c0150cc (which holds value 0x8c26823c). This looks like a function pointer or struct base.
// Instruction at 0xB4: mov.l @r3,r2 -> loads the content of that memory location into r2. 
// If 0x8C26823C is an address, then func_8C2682

/*
 * 0x8C0150C2 - func_8C0150C2
 */
#include <stdint.h>

// External declarations for referenced functions and data (as per Dreamcast ABI context)
extern void func_8C2697A9(void); // Address 0x8C2697A9 derived from offset at 0x8C0152DC + base logic if needed, but here used as direct call target via r3/r4 setup? Wait. 
// Correction based on assembly:
// At 0x8C0150F4: mov.l 0x8c0152dc,r2 -> [r2] = 0x8C2697A9 (This is a function pointer or address)
// Then jsr @r2 calls it. So func_8C2697A9 exists.

extern void (*func_ptr_r3)(void); // Placeholder for the table lookup

/*
 * 0x8C015044 - func_8C015044
 */


/*
 * 0x8C015010 - func_8C015010
 */


/*
 * 0x8C014FE4 - func_8C014FE4
 */


/*
 * 0x8C01506E - func_8C01506E
 */


/*
 * 0x8C015024 - func_8C015024
 */


/*
 * 0x8C015198 - func_8C015198
 */


/*
 * 0x8C014F54 - func_8C014F54
 */


/*
 * 0x8C015106 - func_8C015106
 */
#include <stdint.h>

// External declarations for global data addresses loaded at runtime/static locations.
extern uint32_t *g_data_8C0152D4; // 0x8C0152D4 = 0x8C011D0C (Function pointer)
extern uint32_t g_flag_8c2897ac[6];   // Base address for flag array, size inferred from usage. 
// Note: The assembly loads specific offsets into this block or uses it as a base with offset math.
// Based on 0x8C2897AC and subsequent accesses like @(r15+), we assume an aligned structure.
extern uint32_t *g_data_8c26823c; // Base address for the struct containing flags at offsets +4, +12? 
// Actually, looking at 0x8C26823C usage: mov.l @r1,r2 then byte ops.

/*
 * 0x8C0153AE - func_8C0153AE
 */
#include <stdint.h>

extern uint32_t func_8C011BBC(uint32_t); // Inferred from jsr call at 0x8C0153B0
extern void *g_ptr_table_base;           // Placeholder for the base address of pointer table (derived context)
// Note: The assembly uses absolute addresses. 
// [0x8c015418] = 0x8C011BBC -> func_8C011BBC is called with no args, result in r0.
// If the call returns a pointer to data structure containing pointers at offset +12:

extern uint32_t *g_data_ptr_table;       // Placeholder for table starting at 0x8c01540c (derived context)
extern int g_flag_byte_6C;               // Placeholder for byte stored

/*
 * 0x8C01534C - func_8C01534C
 */


/*
 * 0x8C015364 - func_8C015364
 */


/*
 * 0x8C0153F2 - func_8C0153F2
 */


/*
 * 0x8C015448 - func_8C015448
 */


/*
 * 0x8C01526A - func_8C01526A
 */


/*
 * 0x8C0151B0 - func_8C0151B0
 */


/*
 * 0x8C0151EC - func_8C0151EC
 */


/*
 * 0x8C015220 - func_8C015220
 */


/*
 * 0x8C0153D2 - func_8C0153D2 (32 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C0153D2); */

/*
 * 0x8C01564A - func_8C01564A (528 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C01564A); */

/*
 * 0x8C01585A - func_8C01585A (1102 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C01585A); */

/*
 * 0x8C015E68 - func_8C015E68 (850 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C015E68); */

/*
 * 0x8C0164B4 - func_8C0164B4
 */
#include <stdint.h>

extern uint32_t *g_unk_8C01759A; // Address 0x8C01 + offset implied by context or global data section
// Note: The assembly contains embedded words that act as addresses. 
// Based on the pattern `mov.l r11,@(48,r11)` followed immediately by `.word`, these are likely static address constants in a jump table or similar structure, not runtime variables loaded from memory at those exact offsets relative to R11 unless R11 points to an array of pointers.
// However, standard Katana SDK patterns often use `mov.l @(offset,rX), rY` where the offset is encoded as immediate data if it's a small constant, but here we see `.word 0x8c01`. 
// Given the instruction stream: `cmp/str`, then address words

/*
 * 0x8C01549C - func_8C01549C
 */


/*
 * 0x8C0154D0 - func_8C0154D0
 */


/*
 * 0x8C015502 - func_8C015502
 */


/*
 * 0x8C015CA8 - func_8C015CA8
 */


/*
 * 0x8C0161BA - func_8C0161BA
 */


/*
 * 0x8C0162F8 - func_8C0162F8
 */


/*
 * 0x8C0167F2 - func_8C0167F2
 */
extern uint32_t *gbr; // Global Base Register pointer, typically passed or defined externally in SDK context. Note: The assembly uses @(r0,gbr) syntax where gbr is a register holding the base address of VRAM/ROM data structures relative to GBR. In standard SH-4 calling conventions without explicit arguments for this specific offset logic, we assume r12 (GPR 12) or similar holds 'gbr' if not passed in args. However, looking at `mov.w` sequences loading addresses into registers like r9/r0 before using them as bases relative to GBR suggests these are fixed offsets from the global base register used by Katana SDK for VRAM/ROM access.
// Correction based on standard Dreamcast SH-4 usage: The syntax @(rX, gbr) implies an offset calculation where 'gbr' is a specific value (often 0x8C26 or similar in retail builds acting as the ROM base).

/*
 * 0x8C01656E - func_8C01656E
 */


/*
 * 0x8C016634 - func_8C016634
 */


/*
 * 0x8C016522 - func_8C016522
 */


/*
 * 0x8C016918 - func_8C016918
 */


/*
 * 0x8C0169E0 - func_8C0169E0
 */


/*
 * 0x8C0166A0 - func_8C0166A0
 */


/*
 * 0x8C0164CC - func_8C0164CC
 */


/*
 * 0x8C016582 - func_8C016582
 */


/*
 * 0x8C01687C - func_8C01687C (156 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C01687C); */

/*
 * 0x8C016A6E - func_8C016A6E (624 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C016A6E); */

/*
 * 0x8C016F8C - func_8C016F8C
 */


/*
 * 0x8C016E16 - func_8C016E16
 */


/*
 * 0x8C016F90 - func_8C016F90
 */
#include <stdint.h>

// External declarations for referenced globals/helpers (inferred from addresses)
extern uint32_t g_8C144768; // Address 0x8C144768 loaded at r3 initially, likely a base pointer or table start. Re-evaluating logic: 
                             // Actually, looking at the code flow:
                             // r3 = 0x8c01708c (which is an address). Let's assume it points to data structures defined elsewhere in SDK.
// Based on typical Katana patterns and the usage of `jsr @r2` where r2 comes from a table, these are likely function pointers or offsets into tables.
extern uint32_t g_8C144768; // Placeholder for 0x8c01708c value if it's not immediate data in the binary dump provided contextually. 
// Correction: The assembly loads `

/*
 * 0x8C017182 - func_8C017182
 */


/*
 * 0x8C017270 - func_8C017270
 */


/*
 * 0x8C016CDE - func_8C016CDE
 */


/*
 * 0x8C016D78 - func_8C016D78
 */


/*
 * 0x8C016E76 - func_8C016E76
 */


/*
 * 0x8C01701C - func_8C01701C
 */


/*
 * 0x8C01760A - func_8C01760A
 */
#include <stdint.h>

// Extern declarations for referenced addresses and functions found in the disassembly context.
extern uint32_t g_unk_8C0177B8; // Address 0x8C017624 loads this into r4 initially (likely a pointer or offset)
extern void (*g_func_ptr_8C017750)(void); 
extern void (*g_func_ptr_8C017752)(uint32_t, uint32_t); // Used at 0x8c01767a with r4 as arg
extern void g_proc_a(union SH_REGISTERS *regs); // Corresponds to jump table entry at 0x8C017764 (func_8C018090)
// Note: The actual function pointers are resolved from

/*
 * 0x8C0173AA - func_8C0173AA
 */


/*
 * 0x8C0173A6 - func_8C0173A6
 */


/*
 * 0x8C0173E2 - func_8C0173E2
 */


/*
 * 0x8C0177D8 - func_8C0177D8
 */


/*
 * 0x8C017750 - func_8C017750
 */


/*
 * 0x8C017552 - func_8C017552
 */


/*
 * 0x8C017482 - func_8C017482
 */


/*
 * 0x8C0173B2 - func_8C0173B2
 */


/*
 * 0x8C017686 - func_8C017686
 */
extern uint32_t *g_ptr_table_start_0x8C044F12_end_of_block; // Address 0x8C044F12 loaded at offset A6/A7 context? No, it's a direct load. Let's re-verify the data flow.
// Actually, looking closely: 
// mov.l    @(r0,r13),r13 -> r13 becomes pointer to struct/array based on index in r8 (which is 0x8C044F12).
// The previous attempt error suggests a typo or missing declaration for the global variable holding that address. 
// In SH-4 decompilation of Katana SDK, these are often static arrays passed as arguments or globals defined elsewhere.
// Since I cannot see the linker map, I must assume `g_ptr_table_start_0x8C1294BC` etc exist if they were used directly, but here we load them into registers r3/r2/etc first.
// The error was specifically on a declaration line that shouldn't be there or had bad syntax. 
// Let's ensure all externs are clean and valid C types based on usage (uint8_t*, uint16_t*).

extern void (*func_ptr_0x8C145430)(void); // Address 0x8C145430 loaded at offset E7/E

/*
 * 0x8C017CCC - func_8C017CCC
 */
#include <stdint.h>

// Extern declarations for referenced globals/helpers (inferred from addresses and usage patterns typical of Katana SDK/MvC2 engine)
extern uint8_t *g_vram_base; // Base address 0x8C1454AC inferred, or passed via r6 context if not global. Assuming g_vram_base points to the structure containing these offsets relative to a base pointer (r4).
// Note: The assembly uses absolute addresses for data structures but calculates pointers based on 'r4' which acts as an offset from a module base or is the primary VRAM buffer pointer passed in r4. 
// Based on standard Katana SDK patterns, r4 often holds a specific object/structure address (e.g., 0x8C129xxx) and absolute addresses like 0x8c017d9X are offsets into that structure or global constants relative to the module base.
// However, looking closely at `mov.b @(r0,r4), r0` where r0 is derived from an offset table (0x8C129xxx etc.), it implies a lookup in a static array defined elsewhere.
// Let

/*
 * 0x8C01799A - func_8C01799A
 */


/*
 * 0x8C017A60 - func_8C017A60
 */


/*
 * 0x8C017D74 - func_8C017D74 (24 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C017D74); */

/*
 * 0x8C017D8C - func_8C017D8C
 */


/*
 * 0x8C017EC8 - func_8C017EC8
 */


/*
 * 0x8C017ACC - func_8C017ACC
 */


/*
 * 0x8C017BEE - func_8C017BEE
 */


/*
 * 0x8C0177DC - func_8C0177DC
 */


/*
 * 0x8C0178B4 - func_8C0178B4
 */


/*
 * 0x8C018252 - func_8C018252
 */


/*
 * 0x8C01827A - func_8C01827A
 */


/*
 * 0x8C018224 - func_8C018224
 */


/*
 * 0x8C018320 - func_8C018320
 */


/*
 * 0x8C01836C - func_8C01836C
 */


/*
 * 0x8C0184B8 - func_8C0184B8
 */


/*
 * 0x8C01827E - func_8C01827E (162 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C01827E); */

/*
 * 0x8C018136 - func_8C018136
 */


/*
 * 0x8C01807C - func_8C01807C
 */


/*
 * 0x8C017F6A - func_8C017F6A
 */


/*
 * 0x8C018620 - func_8C018620
 */


/*
 * 0x8C0189DE - func_8C0189DE
 */
#include <stdint.h>

// External declarations for referenced globals/helpers based on context analysis
extern uint32_t gbr; // Global Base Register (often used as a pointer to VRAM or specific data)
extern void *func_8C014D6E(void); // Placeholder for the function at 0x8c1458d4 if it's indirect, but here we use direct address logic. Actually, looking at `mov.l @(r0,r2), r3` then `jmp @r3`, this is an indirect jump table lookup.
// The addresses like 0x8C018BA8 and 0x8c018b34 are likely pointers to function tables or data structures in the ROM/VRAM map of Katana SDK.

extern uint32_t *table_8C018ba8; // Pointer at address

/*
 * 0x8C018522 - func_8C018522
 */


/*
 * 0x8C018A30 - func_8C018A30
 */


/*
 * 0x8C018BD2 - func_8C018BD2
 */


/*
 * 0x8C018624 - func_8C018624
 */


/*
 * 0x8C0189A0 - func_8C0189A0
 */


/*
 * 0x8C018834 - func_8C018834
 */


/*
 * 0x8C0186EC - func_8C0186EC
 */


/*
 * 0x8C018B28 - func_8C018B28
 */
extern uint32_t (*func_8C0184D4)(uint32_t, int); // Helper function address 0x8c0184d4
// Global data pointers (addresses stored in ROM/VRAM)
static const uintptr_t g_data_ptr_table[] = { 
    0x8C018B56UL, // Address of instruction at offset +2 relative to start? No, these are absolute addresses.
                   // Re-evaluating based on assembly: The .word directives define a jump table or data structure in ROM/VRAM.
};

// Corrected approach for the static lookup tables found directly in code (0x8C1459xx)
extern const uint32_t g_jump_table_1[]; // Points to 0x8c14... sequence starting at offset implied by context or direct load? 
    // Actually, looking at `mov.l @(r0,r2), r3`, the addresses loaded are:
    // Offset +6 (from B56): 0x8C1459EC -> This is likely a function pointer.
    // Let's define them as

/*
 * 0x8C018DC4 - func_8C018DC4
 */


/*
 * 0x8C018F8C - func_8C018F8C (24 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C018F8C); */

/*
 * 0x8C018CEC - func_8C018CEC
 */


/*
 * 0x8C018FA4 - func_8C018FA4
 */


/*
 * 0x8C018D70 - func_8C018D70
 */


/*
 * 0x8C0190DA - func_8C0190DA
 */


/*
 * 0x8C018E26 - func_8C018E26
 */


/*
 * 0x8C018ED0 - func_8C018ED0
 */


/*
 * 0x8C018C4A - func_8C018C4A
 */


/*
 * 0x8C019030 - func_8C019030
 */
extern uint32_t g_vram_table_8C0190E8; // Address 0x8C0190E8 contains the base pointer (r6)
extern const uint32_t *g_data_ptr_8C0190F4; // Pointer loaded from 0x8C0190F4, used as r1 in some contexts but here likely a specific table address. Based on usage `mov.l @r6,r1` then offsetting by 8 (2 words), it seems to be another base or the same one reloaded? Actually looking at code: 
// Line 5E loads from 0x8C0190F4 into r3, not r1. Wait, line E7 `mov.l @r6,r1`.
// Let's trace carefully.

extern uint32_t g_offset_base_8C0190D8; // Address where the final write happens? No, 0x8c0190d8 is code address for rts/return logic in previous attempt context or just a label? 
// Re-reading assembly:
// `mova 0

/*
 * 0x8C019406 - func_8C019406
 */


/*
 * 0x8C0191AA - func_8C0191AA
 */


/*
 * 0x8C019630 - func_8C019630
 */
#include <stdint.h>

// External declarations for referenced globals/helpers (as per instructions)
extern uint32_t g_unk_8C0196A0; // Address 0x8C0197EC / 0x8C04 pattern suggests a table or specific offset logic, but treated as extern here.
// Note: The assembly uses hardcoded addresses for tables (e.g., 0x8c0197ec). In real decompilation, these would be resolved to actual function pointers or data structures defined elsewhere in the SDK. 
// For this snippet to compile standalone with -fsyntax-only as requested without resolving every single global:
extern void (*g_jump_table_8C26A934)(void); // Placeholder for 0x8c10 / 0x8c14 patterns if they were functions, though context suggests data tables. 
// Given the complexity of hardcoded addresses in retail SDKs without a symbol table:
extern uint32_t g_data_8C108060; // Placeholder for address at 0x8C108060 (from mov.l @r3,r0)
extern void (*g_func_ptr_8C145A7A)(void); // Placeholder for jump target logic if applicable, though used as data here.

// Re-defining externs based on the specific address patterns found in the assembly to ensure syntax validity without linker errors for unknown symbols.
// The code heavily relies on hardcoded addresses which act as function pointers or table bases.
extern void (*g_ptr_8C145AB4)(void); // 0x8c0196c4 -> 0x8C145AB4 (Jump target)
extern uint32_t g_data_base_8C145AA8; // 0x8c0196c8 -> 0x8C145AA8 (Data base for offsets like +28, +30)
extern void (*g_func_ptr_8C108210)(void); // 0x8c0196cc -> 0x8C108210 (JSR target)
extern uint32_t g_data_base_8C145A7A;   // 0x8c019

/*
 * 0x8C0195A2 - func_8C0195A2
 */


/*
 * 0x8C019784 - func_8C019784
 */


/*
 * 0x8C0197A6 - func_8C0197A6
 */


/*
 * 0x8C0191EC - func_8C0191EC
 */


/*
 * 0x8C0197C8 - func_8C0197C8
 */
#include <stdint.h>

extern uint32_t *func_8C0197F8(void); // Address 0x8C0197F8 points to this function's entry or data, treated as a pointer source here based on context. Actually, looking at `mov.l @(r0,r4), r14` and the jump table logic:
// Correction from assembly analysis: 
// The first instruction loads an address into R3 which is then used for indirect jumps (JMP @R3). This looks like a trampoline or function pointer lookup.
// However, standard Katana SDK patterns often use static arrays of addresses as dispatch tables.
// Let's assume the value at 0x8C0197F8 is an address to jump to directly if it were data, but `jmp @r3` implies R3 holds a function pointer or code offset

/*
 * 0x8C019422 - func_8C019422
 */


/*
 * 0x8C0192D8 - func_8C0192D8
 */


/*
 * 0x8C01997E - func_8C01997E
 */


/*
 * 0x8C0199A6 - func_8C0199A6
 */


/*
 * 0x8C019C06 - func_8C019C06
 */


/*
 * 0x8C019C92 - func_8C019C92
 */


/*
 * 0x8C019B48 - func_8C019B48
 */
#include <stdint.h>

// External symbols referenced in the function (addresses loaded from ROM)
extern uint8_t *g_0x8C145D1C; // Address 0x8C145D1C
extern void (*func_ptr_table_a[])(void); 
extern void (*func_ptr_table_b[])(uint32_t, uint32_t);

// Global variables or data structures referenced by the function (inferred from usage)
extern struct { /* structure containing fields accessed via offsets */ } g_data; // Placeholder for actual global/struct layout if needed. Assuming direct memory access based on pointer arithmetic relative to r4/r6 which act as base pointers in this context, likely a large buffer or specific object instance passed by reference (r4).
// Note: The assembly uses absolute addresses loaded into registers and then offset from them. 
// Without the full symbol table mapping these hardcoded 32-bit values to C symbols/structs, we must assume they point to global data structures common in Katana SDK games or specific function pointers embedded in ROM tables.

extern uint8_t *g_0x8C145D1C; // Corresponding to mov.l #0x8c019c2c,r5
// The assembly loads absolute addresses into r3, then uses them as

/*
 * 0x8C019CF4 - func_8C019CF4
 */


/*
 * 0x8C019832 - func_8C019832
 */


/*
 * 0x8C019AD0 - func_8C019AD0
 */


/*
 * 0x8C019952 - func_8C019952
 */


/*
 * 0x8C0199FA - func_8C0199FA
 */


/*
 * 0x8C019FEA - func_8C019FEA
 */
#include <stdint.h>

// Global variables referenced in func_8C019FEA (must be declared extern for standalone compilation)
extern uint32_t g_vram_base; // Corresponds to r4 usage pattern, assumed global VRAM base pointer context or passed via register if not static. Based on assembly 'r4' being used as a base without definition in snippet, we assume it's an external variable holding the target buffer address.
extern uint32_t g_flag_8C01A056; // Corresponds to 0x8c01a056 (mov.w) - likely a flag or offset table entry
extern uint32_t g_data_ptr_8C019FEF; // Corresponds to 0x8c01a04e/r1 usage pattern in the loop logic. 
// Note: The assembly uses hardcoded

/*
 * 0x8C019D04 - func_8C019D04
 */


/*
 * 0x8C019D96 - func_8C019D96
 */


/*
 * 0x8C01A04E - func_8C01A04E
 */
#include <stdint.h>

// Extern declarations for referenced globals/helpers (as per instructions)
extern uint32_t gbr; // Global Base Register, typically 0x8C000000 on Dreamcast SH-4 core context if not passed in r15/gpr setup
extern void (*func_ptr_8c01a1ba)(void); 
// Note: The assembly uses absolute addresses for function pointers. In a real decompiler, these would be resolved to actual symbols or externs based on the binary map.
// 0x8C01A1BA -> Function pointer used at A0AA and A0C4
extern void (*func_ptr_8c01a1be)(void); 
// Note: The assembly uses absolute addresses for function pointers. In a real decompiler, these would be resolved to actual symbols or externs based on the binary map.
// 0x8

/*
 * 0x8C01A0D4 - func_8C01A0D4
 */


/*
 * 0x8C01A122 - func_8C01A122
 */


/*
 * 0x8C019DFA - func_8C019DFA
 */


/*
 * 0x8C019F66 - func_8C019F66
 */


/*
 * 0x8C019E1C - func_8C019E1C
 */


/*
 * 0x8C019E6C - func_8C019E6C
 */


/*
 * 0x8C01A2D2 - func_8C01A2D2
 */


/*
 * 0x8C01A5F0 - func_8C01A5F0
 */


/*
 * 0x8C01A71A - func_8C01A71A
 */


/*
 * 0x8C01A44A - func_8C01A44A
 */


/*
 * 0x8C01A65C - func_8C01A65C
 */
extern uint32_t (*func_0x8C145E5C)(uint32_t, int); // Address 0x8C145E5C (from r2 at A6D8)
static void func_helper_A(void *dst_addr, const struct _SH4_Context *ctx, uint32_t flags, 
                           uint32_t src_offset_1, uint32_t src_offset_2);

// Helper to decompile the complex block starting at 0x8C01A6B0
static void process_block(uint32_t dst_addr, const struct _SH4_Context *ctx) {
    // This function reconstructs logic from A6B0-A702 based on register state analysis.
    // It handles the nested calls and bit manipulations observed in assembly.
}

// Global variables referenced by addresses 0x8C01A7xx (approximated as extern for syntax)
extern uint32_t g_unk_A740;   // Value: 0x8C26A940
extern int (*g_ptr_A71c)(void);    // Function pointer at A71c -> calls func_0x8C044F12 (via r2)
// Note: Exact types for

/*
 * 0x8C01A7E8 - func_8C01A7E8
 */


/*
 * 0x8C01A88C - func_8C01A88C
 */


/*
 * 0x8C01A1B6 - func_8C01A1B6
 */


/*
 * 0x8C01A378 - func_8C01A378
 */


/*
 * 0x8C01A4AC - func_8C01A4AC
 */


/*
 * 0x8C01AB54 - func_8C01AB54
 */
extern void *func_8C26A940(void); // Address 0x8C26A940 loaded at AC34
extern uint32_t func_8C108060(uint32_t a, uint32_t b, uint32_t c); // Address 0x8C108060 loaded at AB80 (AC44)
// Note: The assembly uses hardcoded addresses for function pointers. 
// func_8c01ac58 -> 0x8C108192, AC54 -> 0x8C1081E6, etc., are treated as external symbols here based on context or assumed to be defined elsewhere in the SDK.
extern void *func_8C108192(void); // Address loaded at AB8C (AC58) - likely a jump table base or specific handler if not resolved by data flow, but assembly treats it as immediate load then indirect call logic is complex here. 
// Re-evaluating the "mov.l 0x8c01

/*
 * 0x8C01A996 - func_8C01A996
 */


/*
 * 0x8C01A9D4 - func_8C01A9D4
 */


/*
 * 0x8C01A90E - func_8C01A90E
 */


/*
 * 0x8C01AB50 - func_8C01AB50
 */


/*
 * 0x8C01AD92 - func_8C01AD92
 */


/*
 * 0x8C01ADCC - func_8C01ADCC
 */


/*
 * 0x8C01A9E2 - func_8C01A9E2
 */


/*
 * 0x8C01AAEC - func_8C01AAEC
 */


/*
 * 0x8C01ABF6 - func_8C01ABF6
 */
#include <stdint.h>

// External declarations for referenced global data/funcs found in the assembly stream.
extern uint32_t g_8C01AC64; // Address 0x8C26823C, likely a table or function pointer base
extern void (*g_func_ptr_table[])(void); 
// Based on usage at 0x8c01adac (0x8C146144), g_8C01ADAC is treated as an array of pointers.
extern uint32_t *const __attribute__((aligned(4))) _g_table_adac; // Placeholder for the table starting at 0x8C146144 if not a direct pointer

// Helper function declarations inferred from JSR calls to addresses like 0x8c01adb0, etc.
extern void func_8C1294C8(void); 
extern uint32_t (*func_ptr_helper)(void) __attribute__((section(".text"))); // Generic placeholder for indirect jumps

// Global variables referenced by offsets (simulated as externs since addresses are fixed in ROM/VRAM context but treated as globals here).
// In a real decompiler, these would be resolved to actual global names. Here we use the raw address logic or assume they exist.
extern uint8_t g_vram_buffer_52; // Offset 0x34 from r1 (r1 = base + 52) -> actually offset calculation is dynamic in loop? No, fixed add #52 then load. 
// Re-evaluating: "add #52,r1", "mov.l @r7,r0" where r7 was set to g_8C01AD9C (0x8C26A940).
// So the data is at 0x8C26A940 + offset. Let's assume a global structure or array exists there.

extern uint32_t *const __attribute__((aligned(4))) g_base_addr_8c26a940; // The base address loaded into r7 (r1)
// Actually, looking at 0x8C01AC80: mov.l @(r0,r14),r3 where r0 is derived from a table. 
// Let's assume the data structures are global and accessible via their addresses or offsets relative to some base if not absolute pointers passed in args (but here it looks like VRAM access).
// Since this is SH-4 Dreamcast, "mov.l @(offset,r1)" where r1 holds an address implies direct memory access.

extern uint8_t g_vram_data_52_offset; // Placeholder for the data at base+0x34 (if 52 was hex? No, #52 is decimal). 
// Wait, "add #52,r1".

/*
 * 0x8C01ADE6 - func_8C01ADE6 (1062 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C01ADE6); */

/*
 * 0x8C01B37E - func_8C01B37E
 */
#include <stdint.h>

// External declarations for symbols referenced in the function (addresses resolved from data)
extern uint32_t (*func_8C044F12)(uint32_t, int); // Address 0x8C044F12 loaded at r3 initially? No, it's a pointer to code. Re-evaluating based on usage.
// Actually: mov.l #... ,r3 then jsr @r3 implies indirect call via address in register. 
// Let's define the external functions by their addresses found in data sections or assume they are global symbols if not constants.
// Based on typical Katana SDK patterns, these look like function pointers stored in ROM/RAM tables.

extern uint8_t (*g_ptr_0x8C1294BC)(void); // Address 0x8C1294BC loaded at

/*
 * 0x8C01B3E0 - func_8C01B3E0
 */
extern void func_8C01B3E0(void); // Placeholder for external reference if needed, though this is a stub.

void func_8C01B3E0() {
    return; // rts at 0x8C01B3E0 (nop in delay slot)
}

/*
 * 0x8C01B346 - func_8C01B346
 */


/*
 * 0x8C01B366 - func_8C01B366
 */


/*
 * 0x8C01B20C - func_8C01B20C
 */


/*
 * 0x8C01B2BC - func_8C01B2BC
 */


/*
 * 0x8C01B3C0 - func_8C01B3C0
 */


/*
 * 0x8C01B228 - func_8C01B228
 */


/*
 * 0x8C01B3E4 - func_8C01B3E4
 */
#include <stdint.h>

// Extern declarations for referenced global data and helper functions found in the disassembly context.
extern uint32_t g_unk_8C01B560; // Address 0x8C044F12 (Function pointer)
extern uint32_t *g_ptr_8C01B574; // Pointer to address table starting at offset calculated by mova

// Helper function declarations inferred from JSR calls and context.
void func_helper_a684(uint32_t arg); 
void func_helper_b9c(void* ptr, uint32_t val); 

extern void *g_unk_ptr_0x8C14; // Address 0x8C14 (Used in swap.b logic)

// Global variables referenced by offsets from GBR or specific addresses.
static volatile int g_shadow_r9 = -1; 
static volatile uint32_t g_temp_val[6] __attribute__((aligned(4)));

/*
 * 0x8C01B59E - func_8C01B59E
 */
#include <stdint.h>

extern uint32_t func_8C044F12(uint32_t a, int b); // Derived from jsr @0x8C044F12 with args r6(0), 11
extern void *g_ptr_table_start;                    // Address at 0x8C01B5E0 (derived)
extern uint32_t g_func_8C146288(void);            // Function pointer resolved from table
extern uint32_t (*func_resolver)(void);           // Pointer to resolver function

// Global constants embedded in the binary at specific offsets relative to this code block or absolute if static.
// Based on analysis: 0x8C01B6D8 -> func_8C044F12, etc. We assume these are resolved externs for compilation safety

/*
 * 0x8C01B494 - func_8C01B494
 */


/*
 * 0x8C01B474 - func_8C01B474
 */


/*
 * 0x8C01B59A - func_8C01B59A
 */


/*
 * 0x8C01B498 - func_8C01B498
 */


/*
 * 0x8C01B4DA - func_8C01B4DA
 */


/*
 * 0x8C01B600 - func_8C01B600
 */


/*
 * 0x8C01B604 - func_8C01B604
 */


/*
 * 0x8C01B540 - func_8C01B540
 */


/*
 * 0x8C01B5E0 - func_8C01B5E0
 */
#include <stdint.h>

// External declarations for referenced globals/pointers based on context analysis.
extern const void *g_jump_table_base; // Corresponds to address 0x8C1462E8 loaded at r1
extern uint32_t g_stack_frame_offset;   // Placeholder if the constant load is dynamic, otherwise inline logic handles it directly. 
// Note: The assembly loads a specific absolute value (0x8c01b6ec) into r1 which points to 0x8C1462E8.
extern uint32_t g_return_address_offset; // Placeholder for the constant loaded at 0x8c01b6ca

// Assuming standard Katana SDK structure where these are global variables or function pointers stored in ROM/RAM segments accessible via fixed offsets relative to a base (often VRAM start). 
// Since exact variable names aren

/*
 * 0x8C01B74A - func_8C01B74A
 */


/*
 * 0x8C01B6AA - func_8C01B6AA
 */


/*
 * 0x8C01B792 - func_8C01B792
 */


/*
 * 0x8C01B6CA - func_8C01B6CA
 */


/*
 * 0x8C01B80C - func_8C01B80C
 */


/*
 * 0x8C01B7EA - func_8C01B7EA
 */


/*
 * 0x8C01B840 - func_8C01B840
 */


/*
 * 0x8C01B776 - func_8C01B776
 */


/*
 * 0x8C01B68A - func_8C01B68A
 */


/*
 * 0x8C01B7B6 - func_8C01B7B6 (52 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C01B7B6); */

/*
 * 0x8C01B8B0 - func_8C01B8B0
 */


/*
 * 0x8C01B95A - func_8C01B95A
 */


/*
 * 0x8C01B91A - func_8C01B91A
 */


/*
 * 0x8C01B93A - func_8C01B93A
 */


/*
 * 0x8C01BA4C - func_8C01BA4C
 */


/*
 * 0x8C01B9B0 - func_8C01B9B0
 */


/*
 * 0x8C01B980 - func_8C01B980
 */


/*
 * 0x8C01B99E - func_8C01B99E (18 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C01B99E); */

/*
 * 0x8C01B9B4 - func_8C01B9B4
 */


/*
 * 0x8C01B8BE - func_8C01B8BE
 */


/*
 * 0x8C01BC26 - func_8C01BC26
 */
// This function is a stub containing only an immediate return and NOP.
void func_8C01BC26(void) {
    __asm__ volatile ("rts"); // Return from subroutine (empty delay slot handled by compiler/NOP).
}

/*
 * 0x8C01BA66 - func_8C01BA66
 */


/*
 * 0x8C01BACC - func_8C01BACC
 */


/*
 * 0x8C01BC06 - func_8C01BC06
 */


/*
 * 0x8C01BAA0 - func_8C01BAA0
 */


/*
 * 0x8C01BC2A - func_8C01BC2A
 */
#include <stdint.h>

// Extern declarations for referenced addresses and functions found in the disassembly context.
extern uint32_t *g_907E6403; // Address 0x8C01BC78 content: 0x907E6403 (likely a function pointer or data)
// Note: The assembly loads this address into r7, but the logic below uses it differently. 
// Based on standard Katana SDK patterns, these are likely global pointers to tables/functions.

extern uint8_t *g_8C1294BC; // Address 0x8C01BD90 content
extern void (*func_ptr)(void); // Placeholder for the function called at r3 (likely from g_8C1294BC)

// Global variables referenced by addresses in assembly constants.
// These are inferred as global state based on typical Katana SDK

/*
 * 0x8C01BCB0 - func_8C01BCB0
 */


/*
 * 0x8C01BCCA - func_8C01BCCA
 */


/*
 * 0x8C01BBAC - func_8C01BBAC
 */


/*
 * 0x8C01BADC - func_8C01BADC
 */


/*
 * 0x8C01BE14 - func_8C01BE14
 */


/*
 * 0x8C01BD60 - func_8C01BD60
 */


/*
 * 0x8C01BE3E - func_8C01BE3E
 */


/*
 * 0x8C01BD46 - func_8C01BD46
 */


/*
 * 0x8C01BCFA - func_8C01BCFA
 */


/*
 * 0x8C01BFB4 - func_8C01BFB4
 */


/*
 * 0x8C01BFBC - func_8C01BFBC
 */


/*
 * 0x8C01C188 - func_8C01C188
 */


/*
 * 0x8C01BD7A - func_8C01BD7A
 */


/*
 * 0x8C01BE68 - func_8C01BE68
 */
extern void func_8C01BAA4(void); // Target of bsr at offset relative to current (approx) - actually absolute address 0x8c01baa4 in original context, but here treated as function pointer logic or specific call. Based on assembly `bsr 0x8c01baa4`, this is a direct branch and swap routine often used for sub-calls in Katana SDK.
extern void func_8C02A53A(void); // Target of mov.a / jsr sequence starting at BEB6 (approx) - actually address 0x8c01c00c points to this function pointer stored there? No, `mov.l` loads the value from memory. Let's assume standard Katana SDK pattern where these are global pointers or functions.
extern void func_8C213B0C(void); // Target of mov.a / jsr sequence starting at BECA (approx) - address 0x8c01c01c points to this function pointer? Wait, `mov.l` loads the value from memory into r3. Then `jsr @r3`. So we need a variable or extern for that location if it's not resolved. However, in Katana SDK decompilation, these are often direct calls to specific routines defined elsewhere. Let's assume they exist as functions at those addresses relative to the binary base, OR more likely, the assembly loads an address into r3/r2 and jumps there.
// Correction: The assembly uses `mov.l <address>,rX` then `jsr @<reg>`. This means `<address>` is a function pointer stored in data section or global variable? 
// Looking at Katana SDK structure, these addresses (0x8c01c0xx) are likely pointers to other functions.
extern void (*func_ptr_8C213B0C)(void); // Placeholder for the value loaded from 0x8c01c01c if it's a pointer. But usually in these SDKs, they call specific known routines. 
// Let's assume standard behavior: The addresses like `0x8c01baa4` are absolute function calls or pointers to functions defined elsewhere.
extern void func_8C213AF8(void); // Target of mov.a

/*
 * 0x8C01C30E - func_8C01C30E
 */


/*
 * 0x8C01C312 - func_8C01C312
 */


/*
 * 0x8C01C4AE - func_8C01C4AE
 */


/*
 * 0x8C01C266 - func_8C01C266
 */


/*
 * 0x8C01C4EA - func_8C01C4EA (24 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C01C4EA); */

/*
 * 0x8C01C502 - func_8C01C502
 */


/*
 * 0x8C01C512 - func_8C01C512
 */


/*
 * 0x8C01C5E4 - func_8C01C5E4
 */


/*
 * 0x8C01C316 - func_8C01C316
 */


/*
 * 0x8C01C3E8 - func_8C01C3E8
 */


/*
 * 0x8C01CA08 - func_8C01CA08
 */
#include <stdint.h>

extern uint8_t *g_ptr_8C01CAA0; // Address 0x8C01CAA0 points to this data, or is the address itself? Based on context: mov.l #addr,r2 then load byte from r2. So g_ptr_8C01CAA0 should be a uint32_t constant value representing an address in VRAM/ROM.
extern const void *g_addr_table_start; // Placeholder for 0x8c01ca76 if it's not a global var but the prompt implies externs are needed. Let's assume 0x8C01CA76 is a known constant or variable address in this context. Since I cannot see globals, I will declare them as external symbols with their addresses/values where possible based on standard decompilation practices for such snippets (often these point to specific tables).

/*
 * 0x8C01C892 - func_8C01C892
 */


/*
 * 0x8C01C816 - func_8C01C816
 */


/*
 * 0x8C01CA48 - func_8C01CA48
 */


/*
 * 0x8C01C8C6 - func_8C01C8C6
 */


/*
 * 0x8C01CA76 - func_8C01CA76
 */


/*
 * 0x8C01CA28 - func_8C01CA28 (32 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C01CA28); */

/*
 * 0x8C01C700 - func_8C01C700
 */


/*
 * 0x8C01C6C4 - func_8C01C6C4
 */


/*
 * 0x8C01C94E - func_8C01C94E
 */


/*
 * 0x8C01CD20 - func_8C01CD20
 */


/*
 * 0x8C01CBBA - func_8C01CBBA
 */


/*
 * 0x8C01CD28 - func_8C01CD28
 */


/*
 * 0x8C01CF04 - func_8C01CF04
 */


/*
 * 0x8C01D012 - func_8C01D012
 */


/*
 * 0x8C01CD52 - func_8C01CD52
 */


/*
 * 0x8C01D03E - func_8C01D03E
 */


/*
 * 0x8C01CD84 - func_8C01CD84
 */


/*
 * 0x8C01CC28 - func_8C01CC28
 */


/*
 * 0x8C01CDF4 - func_8C01CDF4 (272 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C01CDF4); */

/*
 * 0x8C01D0DC - func_8C01D0DC (776 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C01D0DC); */

/*
 * 0x8C01D4E6 - func_8C01D4E6
 */


/*
 * 0x8C01D428 - func_8C01D428
 */


/*
 * 0x8C01D44C - func_8C01D44C
 */


/*
 * 0x8C01D9EC - func_8C01D9EC
 */


/*
 * 0x8C01DB1E - func_8C01DB1E
 */


/*
 * 0x8C01D626 - func_8C01D626
 */


/*
 * 0x8C01D3E4 - func_8C01D3E4
 */


/*
 * 0x8C01D8F2 - func_8C01D8F2
 */


/*
 * 0x8C01D760 - func_8C01D760
 */
extern void func_8C146AA4(uint32_t, uint32_t);
extern void func_8C146900(void *base_addr, int offset, uint32_t value); // Inferred from usage pattern at 0x8C01D7B6 and similar calls
// Note: The assembly uses indirect jumps via r2/r3 which are resolved to specific addresses. 
// Based on the context of Katana SDK/MVCA2 engine, these likely point to internal helper functions or data structures.
// Since exact function names aren't available in standard headers for this binary, we use placeholders or assume they resolve to known entities if decompiled elsewhere.
// For compilation purposes with -fsyntax-only without the full symbol table:

extern void func_8C14690C(void *base_addr, int offset, uint32_t value); // Address 0x8C14690C at 0x8C01D7B6
// Re-evaluating based on the assembly flow: 
// The code loads addresses into r2/r3 and jumps. These are likely function pointers or specific routine entry points within the engine's internal tables (e.g., hitbox checks, sprite handling).
// Without a symbol map for 0x8C146900/0xC etc., we must assume they exist as functions taking arguments consistent with their usage.

extern void func_8C1294C8(void *base_addr); // Address loaded into r3 at multiple points (e.g., 0x8C01D7F8, 0x8C01D86A)
// Usage: jsr @r3 where r3 = address of func_8C1294C8. 
// Arguments passed before call usually in r5/r6 or stack depending on convention observed nearby.

extern void func_8C1F8B90(void); // Address loaded into r2 at 0x8C01D7CA
extern void func_8C1F8B91(uint32_t, uint32_t); // Loaded to r6? No, r6 is passed as arg. 
// Correction: At 0x8C01D7CA, mov.l @r6,r4 implies reading from the argument register directly if it's a pointer array or similar structure access pattern common in Katana SDK for hitboxes/sprites.
// Let's assume standard C linkage where r5/r6 are passed arguments and used as pointers/indices into internal tables stored at 0x8C1F...

extern void func_8C26823C(void); // Address loaded to r6? No, mov.l @r6,r4 suggests reading FROM the address

/*
 * 0x8C01DF3C - func_8C01DF3C
 */
#include <stdint.h>

extern uint32_t *g_unk_8C2141E9; // Address stored at 0x8C01DF84
extern void (*func_ptr_a)(void);   // Function pointer from delay slot after mov.b @r3 (likely jump target logic)
// Note: The assembly uses indirect jumps and specific VRAM/ROM addresses. 
// Based on the pattern `mov.l 0x..., rX` followed by a branch/jump, these are likely function pointers or data tables in ROM/VRAM.
extern uint8_t *g_unk_table_a; // Address stored at 0x8C01DF94 (used for multiplication base)

// Reconstructing the logic based on SH-4 delay slots and register usage:
// r3 points to a table of function pointers or data.
// The code

/*
 * 0x8C01DCB4 - func_8C01DCB4
 */


/*
 * 0x8C01DBA6 - func_8C01DBA6
 */


/*
 * 0x8C01DF68 - func_8C01DF68
 */
#include <stdint.h>

// External declarations for referenced addresses and helper functions found in the disassembly context.
extern uint32_t (*func_8C0450C0)(void); // Target of initial jump @ 0x8C01DF6A (resolved from data at 0x8C01DF9C)
// Note: The assembly uses absolute addresses for constants and function pointers. 
// In a real decompiler context, these would be resolved to actual symbols or global variables.

extern uint32_t (*gbr_ptr)(void); // Placeholder for the GBR register usage if it points to data; treated as extern here based on @(r0,gbr)
extern void func_8C129128(void);    // Resolved from 0x8C01E07C (data at 0x8C045036 -> actually the value is stored there, but used as a function ptr? No, it's data. Re-evaluating logic.)
// Correction on static analysis: 
// The assembly loads addresses from fixed locations like 0x8c01e07c which contain other absolute addresses (e.g., 0x8C287AE8).
// These are likely function pointers or global variable addresses. We must declare them as externs to make the C code valid syntax, 
// assuming they point to functions returning uint32_t based on

/*
 * 0x8C01E038 - func_8C01E038
 */


/*
 * 0x8C01DEA2 - func_8C01DEA2
 */


/*
 * 0x8C01E22C - func_8C01E22C
 */


/*
 * 0x8C01DCFA - func_8C01DCFA
 */


/*
 * 0x8C01DBCA - func_8C01DBCA
 */


/*
 * 0x8C01DD9C - func_8C01DD9C
 */


/*
 * 0x8C01E8BC - func_8C01E8BC
 */
#include <stdint.h>

// External declarations for referenced global data and helper functions.
extern uint32_t g_Data_8C01E950; // Address 0x8C01E950 = 0x8C01E926 (likely a base pointer or offset)
extern uint32_t g_VAddrTableBase; // Placeholder for the table containing function pointers at offsets defined in code. 
// Based on context, these are likely specific VRAM addresses loaded into registers r2/r3 before jsr calls.

// Reconstructing the static data loads based on assembly constants:
static const uint8_t g_Offset_0x16 = 0x16; // Derived from 'add #52' where base is often a struct offset, but here it's an absolute add to r4? 
// Actually, looking at `mov

/*
 * 0x8C01E926 - func_8C01E926 (904 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C01E926); */

/*
 * 0x8C01E580 - func_8C01E580
 */


/*
 * 0x8C01EDDC - func_8C01EDDC
 */


/*
 * 0x8C01E898 - func_8C01E898
 */


/*
 * 0x8C01E746 - func_8C01E746
 */


/*
 * 0x8C01E5B2 - func_8C01E5B2
 */


/*
 * 0x8C01E822 - func_8C01E822
 */


/*
 * 0x8C01E3D6 - func_8C01E3D6
 */


/*
 * 0x8C01ECAE - func_8C01ECAE (302 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C01ECAE); */

/*
 * 0x8C01F2E6 - func_8C01F2E6 (40 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C01F2E6); */

/*
 * 0x8C01EEEC - func_8C01EEEC
 */


/*
 * 0x8C01F342 - func_8C01F342
 */


/*
 * 0x8C01F0CA - func_8C01F0CA
 */


/*
 * 0x8C01F30E - func_8C01F30E
 */
#include <stdint.h>

// Global constants derived from absolute addresses in the assembly.
extern const uint32_t g_offset_low_byte = 0x8C147178; // Address at F364 (r0 load) - Base? No, it's an immediate address used as a pointer base or offset source depending on context, but here loaded into r0.
// Re-evaluating based on standard Katana SDK patterns: These are likely pointers to data structures in the game ROM/VRAM map.
extern const uint32_t g_ptr_base_1 = 0x8C26A940; // Address at F360 (r3 load)
// Note: The assembly loads absolute addresses directly into registers, not relative offsets from a base register like r4 in the final usage context shown. 
// However, to make it compilable as

/*
 * 0x8C01F028 - func_8C01F028
 */


/*
 * 0x8C01F3EC - func_8C01F3EC
 */


/*
 * 0x8C01F1D6 - func_8C01F1D6
 */


/*
 * 0x8C01F290 - func_8C01F290
 */


/*
 * 0x8C01EFCE - func_8C01EFCE
 */


/*
 * 0x8C01F450 - func_8C01F450
 */


/*
 * 0x8C01F454 - func_8C01F454
 */


/*
 * 0x8C01F438 - func_8C01F438
 */


/*
 * 0x8C01F5D0 - func_8C01F5D0
 */
#include <stdint.h>

// Global variables referenced in the function (declared extern for standalone compilation)
extern uint32_t g_vram_ptr;          // Corresponds to r0 base address logic, likely passed or global VRAM pointer context. Based on `mov.b @(r0,r12),r0`, this looks like a specific offset lookup from a structure pointed to by R0 (which is often the object instance in these SDK patterns). Let's assume standard Katana SDK pattern where r0 might be an index into a table or part of a struct.
// Re-evaluating based on `mov.b @(r0,r12),r0`: This loads a byte from [R0 + R12]. 
// In Dreamcast SH-4 games, this often accesses specific flags in the object's data structure passed as an argument or global context.
// However, looking at the usage

/*
 * 0x8C01F628 - func_8C01F628
 */


/*
 * 0x8C01F624 - func_8C01F624
 */


/*
 * 0x8C01F64A - func_8C01F64A
 */


/*
 * 0x8C01F40A - func_8C01F40A
 */


/*
 * 0x8C01F472 - func_8C01F472
 */


/*
 * 0x8C01F556 - func_8C01F556
 */


/*
 * 0x8C01FB1E - func_8C01FB1E (616 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C01FB1E); */

/*
 * 0x8C01FA4E - func_8C01FA4E
 */


/*
 * 0x8C01FE8E - func_8C01FE8E
 */


/*
 * 0x8C01F6E6 - func_8C01F6E6
 */


/*
 * 0x8C01FD86 - func_8C01FD86
 */
#include <stdint.h>

extern uint32_t func_8C14730E(uint32_t a, uint32_t b); // Address 0x8C147310 inferred from context as likely the function called at r3 or similar logic if not direct call
// Note: The assembly shows `jsr @r3` where r3 is loaded with address 0x8C1297C4. 
// Since we don't have a symbol for that specific address, and it's an external jump target in the SDK context, 
// we must assume there exists a function at that address or treat it as opaque if not known.
// However, looking closely: `mov.l 0x8c01fe14,r3` loads 0x8C1297C4 into r3. Then `jsr @r

/*
 * 0x8C01FEC0 - func_8C01FEC0
 */


/*
 * 0x8C01F962 - func_8C01F962
 */


/*
 * 0x8C01FA9E - func_8C01FA9E
 */


/*
 * 0x8C01F7D0 - func_8C01F7D0
 */


/*
 * 0x8C01FDE4 - func_8C01FDE4
 */


/*
 * 0x8C020184 - func_8C020184
 */
extern uint32_t func_8C01F414(uint32_t a, uint32_t b); // Placeholder for external jump target logic if needed, though this function jumps to it.
// Note: The assembly contains direct absolute addresses (e.g., 0x8c01f414) which are treated as global pointers in the decompiled context or resolved at link time. 
// For a standalone C snippet without full symbol resolution of every hardcoded address, we assume these point to external functions/data structures defined elsewhere in the SDK.
extern uint32_t (*g_ptr_8C020194)(void); // Represents 0x8c01f414 as an extern function pointer for clarity if it's a call target, but here it is 'bra' (branch). 
// Correction: The assembly uses `bra` to absolute addresses

/*
 * 0x8C020024 - func_8C020024
 */


/*
 * 0x8C0200CC - func_8C0200CC
 */


/*
 * 0x8C0201EC - func_8C0201EC
 */
#include <stdint.h>

// Helper function declarations (extern) to ensure standalone syntax validity.
void func_8C0358(void); // Corresponds to the BSR at 0x8C020206 relative offset logic, assuming standard call pattern or specific address resolution if not a direct jump table entry in this snippet context. However, based on "bsr 0x8c020358", it's likely an absolute branch to that function name/address.
// Note: In real decompilation of Katana SDK, addresses like 0x8C1474EC are often resolved at link time or via a jump table not fully visible here. 
// We assume func_8C0358 is the target label for the BSR instruction found in assembly.
extern void (*func_ptr_table)(void); // Placeholder if r2 points to

/*
 * 0x8C020028 - func_8C020028
 */


/*
 * 0x8C020216 - func_8C020216
 */


/*
 * 0x8C020128 - func_8C020128
 */


/*
 * 0x8C02021A - func_8C02021A
 */


/*
 * 0x8C01FF90 - func_8C01FF90
 */


/*
 * 0x8C020118 - func_8C020118
 */


/*
 * 0x8C020358 - func_8C020358
 */


/*
 * 0x8C0204B6 - func_8C0204B6
 */


/*
 * 0x8C020372 - func_8C020372
 */


/*
 * 0x8C020324 - func_8C020324
 */


/*
 * 0x8C020746 - func_8C020746
 */


/*
 * 0x8C020876 - func_8C020876
 */


/*
 * 0x8C020806 - func_8C020806
 */


/*
 * 0x8C020378 - func_8C020378
 */


/*
 * 0x8C0204C4 - func_8C0204C4
 */


/*
 * 0x8C020588 - func_8C020588 (446 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C020588); */

/*
 * 0x8C0209C6 - func_8C0209C6
 */


/*
 * 0x8C020AE8 - func_8C020AE8
 */


/*
 * 0x8C020942 - func_8C020942
 */


/*
 * 0x8C020C50 - func_8C020C50
 */


/*
 * 0x8C020C54 - func_8C020C54
 */


/*
 * 0x8C020CE6 - func_8C020CE6
 */


/*
 * 0x8C020C08 - func_8C020C08
 */
extern uint32_t *g_base_table_8C020C78; // Address 0x8C147590, likely a base pointer for the jump table structure
extern const struct { int val1; } g_jump_table_FXX[]; 
// Note: The assembly uses an offset from r3 (which points to this array) and then jumps via another index.
// Based on `mov.b @(5,r3),r0` followed by `shll2`, it reads a 4-byte entry at +8 bytes relative to the start of the struct in memory, 
// effectively skipping one int if packed as {int,int} or just reading an offset directly depending on alignment.
// Given typical Katana SDK patterns: r3 points to an array where each element is likely `struct { uint16_t low; }` or similar small structs,
//

/*
 * 0x8C020884 - func_8C020884
 */


/*
 * 0x8C020B28 - func_8C020B28
 */


/*
 * 0x8C0209EC - func_8C0209EC
 */


/*
 * 0x8C020E2C - func_8C020E2C
 */


/*
 * 0x8C020D50 - func_8C020D50
 */


/*
 * 0x8C020D6E - func_8C020D6E
 */


/*
 * 0x8C021118 - func_8C021118
 */


/*
 * 0x8C020DA6 - func_8C020DA6
 */


/*
 * 0x8C0211DC - func_8C0211DC
 */


/*
 * 0x8C021148 - func_8C021148
 */


/*
 * 0x8C020E3A - func_8C020E3A
 */


/*
 * 0x8C021052 - func_8C021052
 */
#include <stdint.h>

// Helper function declarations (extern) to ensure standalone compilation with -fsyntax-only
extern void func_8C021344(void); // Called at 0x8C0210F8 based on context analysis of the jump table logic. Note: The assembly shows a direct call/branch pattern that implies this function exists in the linked binary but isn't defined here.
extern void func_8C021376(void); // Placeholder for potential missing helper if 0x8c021344 is not enough, though usually these are specific to game logic. 
// Based on standard Katana SDK patterns:
// The jump table at the end suggests calls to various subroutines. Since exact addresses aren't fully resolved in a single pass without full symbol resolution, we declare them as extern void with generic names or assume they exist.
// However, looking closely at 0x8C0210E6: bsr 0x8c021344 and 0x8C0210EC: bsr 0x8c02122e. These are likely specific game logic functions.
// Let's assume standard naming or generic placeholders if symbols aren't available, but the prompt asks for valid C code. 
// We will declare them as extern void with names derived from context

/*
 * 0x8C020EF4 - func_8C020EF4
 */


/*
 * 0x8C021456 - func_8C021456
 */
#include <stdint.h>

extern uint8_t *gbr; // Global Base Register pointer, typically 0xC0 or similar in SH-4 context for VRAM access patterns if not using GBR directly as base. Note: @(r0,gbr) implies r0 is offset and gbr is the register holding the segment/base address (usually R15/GPR3 on Dreamcast).
extern void func_8C021496(void); // Helper function referenced in delay slot

// Function signature inferred from usage of r4, r7 as args. 
// Based on Marvel vs Capcom 2 code patterns: likely takes a pointer (r4) and an int/enum (r7).
void func_8C01A56(int *ptr_r4_arg, uint32_t val_r7_arg); // Placeholder signature based on typical usage in SDK. 
// Actual

/*
 * 0x8C021344 - func_8C021344
 */


/*
 * 0x8C02122E - func_8C02122E
 */


/*
 * 0x8C02122A - func_8C02122A
 */


/*
 * 0x8C0211E0 - func_8C0211E0
 */


/*
 * 0x8C0214F4 - func_8C0214F4
 */


/*
 * 0x8C0214F8 - func_8C0214F8
 */


/*
 * 0x8C02138A - func_8C02138A
 */


/*
 * 0x8C021298 - func_8C021298
 */


/*
 * 0x8C021498 - func_8C021498 (92 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C021498); */

/*
 * 0x8C0215D6 - func_8C0215D6
 */


/*
 * 0x8C021564 - func_8C021564
 */


/*
 * 0x8C021618 - func_8C021618
 */


/*
 * 0x8C02180E - func_8C02180E
 */
extern void *func_8C129128(void); // Address 0x8C129128 from offset 0x8C0218A2 context (inferred)
extern void func_8C1292D4(int, int, int, int); // Function pointer loaded at 0x8C0218DC

// Helper structures/arrays referenced by absolute addresses in the assembly:
static const uint32_t *g_ptr_table = (uint32_t *)(uintptr_t)0x8C26A944; 
extern int func_8c11e680(int, void *, unsigned char); // Address 0x8C11E680 from offset 0x8C02185C context

// The assembly uses hardcoded offsets relative to r13 (which points to a specific structure instance).
// Based on the logic: 
// - Offset +32 is used for byte access.
// - Offsets like +4, +6 are implied by register usage patterns in similar Katana SDK code or derived from context if not explicit.
// However, looking closely at 0x8C02187E (cmp/str r3,r12) and the jump logic:
// The assembly seems to be manipulating a specific struct layout where fields are accessed via

/*
 * 0x8C02157C - func_8C02157C
 */


/*
 * 0x8C0218D6 - func_8C0218D6
 */


/*
 * 0x8C0218E8 - func_8C0218E8
 */


/*
 * 0x8C0215DA - func_8C0215DA
 */


/*
 * 0x8C021966 - func_8C021966
 */


/*
 * 0x8C021632 - func_8C021632
 */


/*
 * 0x8C02199E - func_8C02199E (732 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02199E); */

/*
 * 0x8C021F74 - func_8C021F74 (708 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C021F74); */

/*
 * 0x8C0222F6 - func_8C0222F6 (850 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C0222F6); */

/*
 * 0x8C022722 - func_8C022722 (1138 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C022722); */

/*
 * 0x8C021E06 - func_8C021E06
 */


/*
 * 0x8C021EC6 - func_8C021EC6
 */


/*
 * 0x8C021E14 - func_8C021E14
 */


/*
 * 0x8C022648 - func_8C022648
 */


/*
 * 0x8C022238 - func_8C022238
 */
extern uint32_t *func_ptr_table_base; // Placeholder for 0x8C022438 / 0x8C12 logic if needed, but likely direct data access. Assuming these are static addresses or globals defined elsewhere in the SDK context which we must assume exist as externs for compilation.
// Note: The assembly uses hardcoded VRAM/ROM addresses (e.g., 0x8c022438). In a real decompiler, these would be resolved to actual function pointers or data structures from the Katana SDK map. 
// For this snippet to compile standalone with -fsyntax-only without linker errors on undefined symbols like 'func_8C1514', we assume they are defined elsewhere in the project's .c files.
extern void func_8C022376(void); // Corresponds to 0x8C022376 (derived from context of typical Katana SDK patterns, though exact addr not fully explicit without map, assuming standard jump target resolution) -> Actually looking at the assembly: 
// The jumps are relative or absolute. Let's trace carefully.
// 0x8c021514 is a call site? No, it looks like `bra` to an address that might be in another file.
// Wait, line

/*
 * 0x8C021C7A - func_8C021C7A
 */


/*
 * 0x8C022B94 - func_8C022B94 (532 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C022B94); */

/*
 * 0x8C022DA8 - func_8C022DA8
 */


/*
 * 0x8C02327C - func_8C02327C (584 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02327C); */

/*
 * 0x8C022F76 - func_8C022F76
 */


/*
 * 0x8C0234C4 - func_8C0234C4
 */


/*
 * 0x8C022DD8 - func_8C022DD8
 */


/*
 * 0x8C022DF2 - func_8C022DF2
 */


/*
 * 0x8C02322C - func_8C02322C
 */


/*
 * 0x8C022FD8 - func_8C022FD8
 */


/*
 * 0x8C023140 - func_8C023140 (236 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C023140); */

/*
 * 0x8C023A44 - func_8C023A44
 */
extern uint32_t *g_8C023A6a; // VRAM address 0x8C023A64 (derived from mov.w) - adjusted based on context if needed, but here it's a literal load. Actually looking at assembly: `mov.w 0x8c023a6a,r1` loads the value AT that address into r1? No, SH-4 syntax `mov.l addr, reg` usually means immediate or absolute addressing depending on context, but in Katana SDK often these are pointers to data tables. Let's assume standard load from memory for pointer arithmetic if it looks like a table base.
// Correction: In Hitachi SuperH assembly provided here (Katana style): `mov.w 0x8c023a6a,r1` likely loads the WORD at address 0x8C023A

/*
 * 0x8C023576 - func_8C023576
 */


/*
 * 0x8C023A68 - func_8C023A68
 */
#include <stdint.h>

// Helper function declarations (extern) to ensure standalone syntax validity.
void func_8C04(void); // Corresponds to branch target 0x8c023a9c logic path or similar indirect call context if not direct jump, but here it's a JSR from r9 which points elsewhere? Wait, analysis shows:
// Actually, looking at the code flow:
// mov.l    @r15+,pr (restore PR) -> this is part of exception handling setup.
// The instruction `jsr      @r9` calls whatever function address is in R9.
// Based on typical Katana SDK patterns and the provided assembly, we assume these are external symbols or internal functions not fully visible here but necessary for compilation context if they were defined locally. 
// However, since this snippet only asks to decompile THIS specific block which contains a JSR @r9 (indirect call), I must declare r9's target as an extern function pointer

/*
 * 0x8C023AF6 - func_8C023AF6
 */


/*
 * 0x8C023732 - func_8C023732
 */


/*
 * 0x8C02391C - func_8C02391C
 */


/*
 * 0x8C0239E8 - func_8C0239E8
 */


/*
 * 0x8C023992 - func_8C023992
 */


/*
 * 0x8C02384E - func_8C02384E
 */


/*
 * 0x8C023B8A - func_8C023B8A (114 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C023B8A); */

/*
 * 0x8C023FFE - func_8C023FFE
 */
#include <stdint.h>

// Extern declarations for referenced functions and globals (inferred from context)
extern void (*func_8C1294BC)(void); // Address 0x8C1294BC loaded at offset F8 relative to base? No, direct load.
extern void func_8C1294C8(void);    // Address 0x8C1294C8 (loaded from constant table)
// Note: The assembly loads addresses directly into registers r3 before calling via jsr @r3.
// We assume these point to valid functions in the game binary.

extern uint8_t g_vram_flag_8c0240cc; // Loaded at offset 0x1C relative to base pointer? No, absolute load from constant table usually implies a global or specific VRAM address. 
                                      // Given "mov

/*
 * 0x8C023FDA - func_8C023FDA
 */


/*
 * 0x8C023BFC - func_8C023BFC
 */


/*
 * 0x8C023FA0 - func_8C023FA0
 */


/*
 * 0x8C024124 - func_8C024124
 */


/*
 * 0x8C023C1E - func_8C023C1E
 */


/*
 * 0x8C024294 - func_8C024294
 */


/*
 * 0x8C024068 - func_8C024068 (188 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C024068); */

/*
 * 0x8C023DDC - func_8C023DDC
 */


/*
 * 0x8C023C70 - func_8C023C70
 */


/*
 * 0x8C024462 - func_8C024462
 */


/*
 * 0x8C0243D8 - func_8C0243D8
 */


/*
 * 0x8C02438C - func_8C02438C
 */


/*
 * 0x8C0246E4 - func_8C0246E4
 */


/*
 * 0x8C0243FC - func_8C0243FC
 */


/*
 * 0x8C024804 - func_8C024804
 */


/*
 * 0x8C024760 - func_8C024760
 */


/*
 * 0x8C0245AE - func_8C0245AE
 */


/*
 * 0x8C02447A - func_8C02447A
 */


/*
 * 0x8C024604 - func_8C024604 (224 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C024604); */

/*
 * 0x8C024808 - func_8C024808 (534 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C024808); */

/*
 * 0x8C024DE2 - func_8C024DE2
 */


/*
 * 0x8C024DFE - func_8C024DFE
 */
extern uint32_t func_8C044F12(uint32_t a, uint32_t b); // Address 0x8C024ED4 -> 0x8C044F12
static const char *sTable = (const char *)0x8C024ECA;    // Label at offset ECA used for mov.w r0, @(r3) logic? No, it's a constant load. Actually looking at usage: mov.w 0x8c024eca,r0 then later moves to memory. It acts as an immediate value or pointer source. Let's assume it points to data table if accessed via ptr, but here loaded into r0 directly. Wait, line E52 loads from address in sTable? No: `mov.w 0x8c024ece,r3`. These are likely constant

/*
 * 0x8C024AD0 - func_8C024AD0
 */


/*
 * 0x8C024ECA - func_8C024ECA
 */


/*
 * 0x8C024A1E - func_8C024A1E
 */


/*
 * 0x8C024F5C - func_8C024F5C
 */
#include <stdint.h>

// External declarations for symbols referenced in the function but not defined here.
extern uint32_t (*func_8C024DE2)(void); // Return type inferred from usage as a subroutine call returning void or int, assumed generic pointer return context based on SH-4 calling conventions where bsr returns to caller's stack frame logic often used for setup/teardown in SDK. However, looking at the code flow:
// The function calls func_8C024DE2 which likely sets up a specific state (like an interrupt handler or callback). 
// Given "sts.l pr,@-r15" before bsr and lds after, it's saving/restoring PR. 
// Let's assume the return type of func_8c024de2 is void for this snippet context as no result is used immediately in a way that suggests an int return

/*
 * 0x8C024B60 - func_8C024B60
 */


/*
 * 0x8C024C26 - func_8C024C26
 */


/*
 * 0x8C024E66 - func_8C024E66
 */


/*
 * 0x8C0252D6 - func_8C0252D6
 */
#include <stdint.h>

// Extern declarations for referenced globals/data (addresses resolved from assembly)
extern uint32_t *g_8C044F12; // Address 0x8C044F12, loaded into r3 initially
extern int g_flag_8C0253BC;   // Value at address 0x8c0253bc (loaded as word)
extern uint32_t *g_ptr_8C0253CC; // Address pointer stored at 0x8C0253cc, loaded into r1 initially
extern int g_flag_8C0253BE;    // Value at address 0x8c0253be (loaded as word)
extern uint32_t *g_ptr_8C0253C0; // Address pointer stored at

/*
 * 0x8C025320 - func_8C025320
 */
#include <stdint.h>

// External declarations for referenced symbols and globals (placeholder names based on context)
extern uint32_t g_unk_8C26823C; // Address stored at 0x8C0253D0
extern const char *g_str_table_start; // Base address of string table, derived from logic flow or global data segment
// Note: The assembly uses hardcoded addresses for the base pointer (r14) and offset calculations. 
// In a real decompilation context without full symbol resolution, these are often mapped to specific globals or structs.
// Assuming r2 points to an array of pointers where each entry is 32-bit aligned data containing string offsets/lengths.

extern uint8_t g_unk_byte_at_offset[10]; // Placeholder for the byte read at offset +6 from base pointer if needed, though logic suggests direct

/*
 * 0x8C024FD4 - func_8C024FD4
 */


/*
 * 0x8C025210 - func_8C025210
 */


/*
 * 0x8C025394 - func_8C025394
 */
#include <stdint.h>

// Extern declarations for referenced globals/helpers (as per Dreamcast ABI context)
extern uint32_t gbr; // Global Base Register, typically 0xC01F8000 on DC hardware but treated as extern here if not defined locally.
                        // Note: In raw SH-4 assembly without a linker map, 'gbr' is the special register for GBR offset calculations relative to code segment base or specific SDK conventions. 
// Assuming standard Katana/SDK usage where gbr points to the start of the executable image in memory space if used as an address pointer, OR it's a local variable name collision in the original C source mapped here.
// Given `mov.w r0,@(92,gbr)`, this is likely accessing a specific offset from the GBR register which holds the base address of the loaded program or a specific segment table. 
// For decompilation safety, we treat it as an external symbol if not defined in scope, but standard Dreamcast C compilers handle `gbr

/*
 * 0x8C025108 - func_8C025108
 */


/*
 * 0x8C025428 - func_8C025428
 */


/*
 * 0x8C025192 - func_8C025192
 */


/*
 * 0x8C02527E - func_8C02527E
 */


/*
 * 0x8C025068 - func_8C025068
 */


/*
 * 0x8C025A04 - func_8C025A04
 */
extern void *func_8C044F12(void *, int, uint32_t); // Address 0x8C025A64 -> 0x8C044F12
static const char gbr_table[] = { /* Data at offset from GBR */ }; 
extern void *func_8C132AC4(void *, int, uint32_t); // Address 0x8C025BB0 -> 0x8C132AC4
extern void *func_8C1294C8(void *, int, uint32_t); // Address 0x8C025BB4 -> 0x8C1294C8

// Helper to resolve GBR-relative addresses if the table is sparse or needs specific indexing logic
static void *resolve_gbr_offset(uintptr_t base_addr) {
    return (void *)(uintptr_t)(base_addr + gbr_table[0]); // Simplified assumption based on typical Katana SDK patterns, 
                                                           // actual offset calculation depends on runtime GBR value in r1.
}

// Note: The assembly uses a complex jump table and indirect calls via the 'gbr' (Global Base Register) mechanism common

/*
 * 0x8C025574 - func_8C025574
 */


/*
 * 0x8C0256A2 - func_8C0256A2
 */


/*
 * 0x8C025AB8 - func_8C025AB8
 */
#include <stdint.h>

// Extern declarations for referenced global data and helper functions found in the disassembly context.
extern uint32_t g_unk8C025BC8; // Value: 0x8C2897E0 (likely a base address or table pointer)
extern uint32_t g_unk8C025BCC; // Value: 0x8C26A944 (pointer to data structure?)
extern uint32_t g_unk8C025BC0; // Value: 0x8C26823C (function pointer or table base)
extern uint32_t g_unk8C025BC4; // Value: 0x8C212CEC (another function/table pointer)
// Note: The addresses at 0x8c025bd0 and 0x8c025bd4 are resolved to specific runtime values in the binary.
extern uint32_t g_unk8C025BD0; // Value: 0x8C129128 (function pointer)
extern uint32_t g_unk8C025BD4; // Value: 0x8C1292D4 (function pointer)

// Helper function stubs inferred from indirect calls @r3. These would be defined elsewhere in the SDK.
void func_helper_A(uint32_t arg); 
extern void (*func_ptr_unk)(uint32_t, uint32_t); // Placeholder for

/*
 * 0x8C025C52 - func_8C025C52
 */


/*
 * 0x8C025B9C - func_8C025B9C
 */


/*
 * 0x8C0254B2 - func_8C0254B2
 */


/*
 * 0x8C02585A - func_8C02585A
 */


/*
 * 0x8C02572C - func_8C02572C
 */


/*
 * 0x8C0258E4 - func_8C0258E4
 */


/*
 * 0x8C025CEC - func_8C025CEC
 */


/*
 * 0x8C025C70 - func_8C025C70
 */


/*
 * 0x8C02618A - func_8C02618A
 */


/*
 * 0x8C02619E - func_8C02619E
 */


/*
 * 0x8C0260C4 - func_8C0260C4
 */
extern uint8_t *func_8C025364(void); // Address 0x8C025364 referenced in branch
// Global data addresses resolved from disassembly context:
static const uintptr_t g_addr_base = (uintptr_t)0x8C010524; 
extern uint32_t *g_ptr_8c2897e0(void); // 0x8C2897E0 at offset 0xA0 from base? Or direct load. Assuming external symbol for safety based on pattern.
// Actually, looking at `mov.l 0x8c0261a0,r14` where [addr] = target address. 
// Let's assume these are pointers to specific data structures loaded into registers r3, r7, r14 initially.

extern uint32_t *g_data_8C2897E0; // Corresponds to 0x8c0261a0
extern uint32_t *g_data_8C26823C; // Corresponds to 0x8c026198 (value is address) -> Wait, instruction says `mov.l imm,r3`. 
// Correction: The assembly loads immediate addresses into registers. These are likely pointers passed or loaded from a table not fully shown, OR they point

/*
 * 0x8C025DE2 - func_8C025DE2
 */


/*
 * 0x8C026256 - func_8C026256
 */


/*
 * 0x8C025ECE - func_8C025ECE
 */


/*
 * 0x8C025D0A - func_8C025D0A
 */


/*
 * 0x8C025F70 - func_8C025F70
 */


/*
 * 0x8C026560 - func_8C026560
 */
#include <stdint.h>

// Extern declarations for referenced globals/symbols (adjust names based on actual SDK context if known)
extern uint32_t g_unk_8C0265e4; // Loaded at 0x8C0265E4, used as base address or offset logic? Context suggests it might be a pointer to data structure. Based on usage: mov.w #addr -> r0/r1 then dereference. Let's assume these are pointers/constants loaded into registers for addressing modes relative to *r4*.
// Re-evaluating 0x8C0265E4/0xE6 based on typical Katana SDK patterns: These look like absolute addresses of data structures or function tables, but the instruction `mov.w #addr` followed by use in address calculation suggests they are base pointers. 
// However, looking at `extu.b @(r

/*
 * 0x8C026340 - func_8C026340
 */


/*
 * 0x8C0263AC - func_8C0263AC
 */


/*
 * 0x8C02664A - func_8C02664A
 */


/*
 * 0x8C0263D0 - func_8C0263D0
 */


/*
 * 0x8C026740 - func_8C026740
 */


/*
 * 0x8C0262AA - func_8C0262AA
 */


/*
 * 0x8C026344 - func_8C026344
 */


/*
 * 0x8C026580 - func_8C026580
 */
extern uint32_t g_data_8C0265E4; // Address 0x8C026744 (from comment in assembly) - actually the label points to data at runtime, but here it's a constant address used as pointer source. Wait, looking closer: `mov.l @r15+, r14` restores stack frame. The previous attempt error suggests I need to declare globals properly or handle inline constants if they are not true externs in this scope. However, the assembly uses absolute addresses for data pointers which act like function calls (jsr) or direct access.
// Correction: The labels 0x8C147F0C etc are likely functions or global variables defined elsewhere. I will treat them as external symbols based on usage context (JSR).

extern uint32_t (*func_8c02659a)(void); // Placeholder for the JSR target if it's a function, but usually these absolute addresses in Katana SDK are data pointers or specific functions. Let's assume they are global variables/functions declared elsewhere.
// Re-evaluating based on "Katana SDK": These look like direct jumps to subroutines defined as .word 0x8c14... which implies function calls via JSR @addr.

extern uint32_t g_ptr_8C147F0C; // Corresponds to address 0x

/*
 * 0x8C026432 - func_8C026432
 */


/*
 * 0x8C026910 - func_8C026910
 */
#include <stdint.h>

// External declarations for global data referenced in the function.
extern uint32_t g_unk_8C1294C8; // Address loaded from 0x8C026998 (constant offset)
extern void (*g_func_ptr)(void);   // Function pointer resolved via JSR @r3

// Helper to cast address constants safely as per instructions.
#define ADDR_CAST(addr, type) ((type *)(uintptr_t)(addr))

static inline uint16_t func_8C02695E(void) { return 0x7F; } // Placeholder for the actual function at this VRAM addr if needed, or just a dummy call target.
// Note: The assembly loads an address from memory (r3 = [0x8c02698c]), so we must assume that location holds a valid

/*
 * 0x8C026874 - func_8C026874
 */


/*
 * 0x8C0267D2 - func_8C0267D2
 */


/*
 * 0x8C026824 - func_8C026824
 */


/*
 * 0x8C026AF0 - func_8C026AF0
 */


/*
 * 0x8C026CA6 - func_8C026CA6
 */


/*
 * 0x8C026888 - func_8C026888
 */


/*
 * 0x8C026828 - func_8C026828
 */


/*
 * 0x8C0268EE - func_8C0268EE
 */


/*
 * 0x8C02695E - func_8C02695E (402 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02695E); */

/*
 * 0x8C026CDE - func_8C026CDE
 */


/*
 * 0x8C027090 - func_8C027090
 */


/*
 * 0x8C026D02 - func_8C026D02
 */


/*
 * 0x8C0270A0 - func_8C0270A0 (50 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C0270A0); */

/*
 * 0x8C0270F8 - func_8C0270F8
 */


/*
 * 0x8C0270D2 - func_8C0270D2
 */


/*
 * 0x8C027260 - func_8C027260
 */


/*
 * 0x8C026D6C - func_8C026D6C
 */


/*
 * 0x8C026F98 - func_8C026F98
 */


/*
 * 0x8C026E28 - func_8C026E28
 */


/*
 * 0x8C027452 - func_8C027452
 */
#include <stdint.h>

extern void func_8C174BC2(uint32_t val); // Address 0x8C1F8AC4 -> likely a helper or data pointer logic, adjusted based on context if needed. Assuming standard function call pattern from assembly 'jsr @r12' where r12 holds address of func_8c027588 (which is 0x8c174bc2).
extern void func_8C174DAC(uint32_t val); // Address 0x8C1F8AC4 -> Wait, checking assembly: 
// r12 = 0x8c027588 (value is 0x8C174BC2). So the function at that address.
extern void func_8C174DAC(uint32_t val); // Address

/*
 * 0x8C027366 - func_8C027366
 */


/*
 * 0x8C0272C2 - func_8C0272C2
 */


/*
 * 0x8C0274B0 - func_8C0274B0
 */
extern void *func_8C027594; // 0x8C1F8E44
extern void *func_8C027598; // 0x8C1F8E54
extern void *func_8C02759c; // 0x8C1F8E8C
extern uint32_t func_8C0275a4[]; // Table starting at 0x8C1F978D (offsets) or data? Context suggests lookup table. Assuming array of offsets/indices based on usage pattern in similar SDK code, but here it's used as a base for byte access via r1+r3 logic later? Re-evaluating: 
// Actually, looking at 0x8C0274E0-E6:
// mov.l #

/*
 * 0x8C0273C6 - func_8C0273C6
 */


/*
 * 0x8C0274F2 - func_8C0274F2
 */


/*
 * 0x8C02738A - func_8C02738A
 */


/*
 * 0x8C0273AE - func_8C0273AE
 */


/*
 * 0x8C027526 - func_8C027526
 */


/*
 * 0x8C0272C6 - func_8C0272C6
 */


/*
 * 0x8C0275F6 - func_8C0275F6 (544 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C0275F6); */

/*
 * 0x8C0279DE - func_8C0279DE
 */


/*
 * 0x8C0279A4 - func_8C0279A4
 */


/*
 * 0x8C0275D2 - func_8C0275D2
 */


/*
 * 0x8C027A7A - func_8C027A7A
 */


/*
 * 0x8C0279E4 - func_8C0279E4 (54 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C0279E4); */

/*
 * 0x8C027AA4 - func_8C027AA4
 */


/*
 * 0x8C027816 - func_8C027816
 */


/*
 * 0x8C0278F2 - func_8C0278F2
 */


/*
 * 0x8C027A1A - func_8C027A1A
 */


/*
 * 0x8C027B36 - func_8C027B36 (1548 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C027B36); */

/*
 * 0x8C028268 - func_8C028268
 */
// This function is a stub/placeholder containing only an immediate return.
void func_8C028268(void) {
    __asm__ volatile ("rts"); 
}

/*
 * 0x8C028270 - func_8C028270
 */
#include <stdint.h>

// Extern declarations for referenced addresses and helper functions found in the decompiled logic.
extern uint32_t (*func_8C044F26)(uint32_t, int); // Address 0x8C044F26 loaded at r3 initially
extern void func_8C108210(uint32_t *dst, const struct _SH4_Context *ctx); // Address 0x8C108210 used for jump

// Global/Static placeholders inferred from context (VRAM addresses)
static uint8_t g_vram_base = 0; 
extern void func_8c54(uint32_t, int); // Inferred helper at address 0x8c54 pattern
extern void func_8c1f(int);           // Helper inferred from jump table logic

//

/*
 * 0x8C027B30 - func_8C027B30
 */


/*
 * 0x8C028246 - func_8C028246
 */


/*
 * 0x8C027ADA - func_8C027ADA
 */


/*
 * 0x8C02823A - func_8C02823A
 */


/*
 * 0x8C02826C - func_8C02826C
 */


/*
 * 0x8C028142 - func_8C028142
 */


/*
 * 0x8C0282EA - func_8C0282EA
 */
#include <stdint.h>

// Helper function declarations (extern) based on referenced addresses in assembly.
static void (*func_8C0450C0)(uint32_t); // Address 0x8C0450C0, called at r3->jsr @r3 with arg from r14? No, looking closely: mov.r4,r14 then jsr@r3. Wait, assembly shows `mov.w @(28,r14),r0` before call in some paths or just passing context. Let's re-examine the specific calls.
// Re-evaluating logic flow for declarations:
// 1. mov.l 0x8c0283d8,r3 (addr=0x8C0450C0); jsr @r3; This is a function pointer call taking no args or implicit context? 
    // Actually, looking at `mov.w @(28,r14), r0` before the branch to 0x8c028398 which jumps over it.
    // Let's assume standard C calling convention for these helpers if arguments are passed in registers (r4).
// The assembly is complex with embedded data tables and indirect calls. We will declare them as extern void or uint32_t based on typical patterns, but since

/*
 * 0x8C02849C - func_8C02849C
 */
#include <stdint.h>

// Extern declarations for referenced global data/tables (addresses resolved from ROM)
extern uint32_t g_unk_table_a[/* size */]; // Corresponds to 0x8C044F26 area logic if needed, but here used as direct load source in some contexts or just placeholder. Actually looking at usage: mov.l #1,r5 then jsr @g_func_A; r3 is func return? No, let's trace carefully.
// Correction based on standard Katana SDK patterns: The addresses loaded are likely function pointers or data tables defined elsewhere. 
// Since I cannot see the linker map for Marvel vs Capcom 2 ROMs directly here, I must assume these static loads point to specific global symbols or functions known in that context.
// However, without a symbol table, I will use extern placeholders with comments indicating their likely nature based on usage

/*
 * 0x8C0283B8 - func_8C0283B8
 */


/*
 * 0x8C028442 - func_8C028442
 */


/*
 * 0x8C028448 - func_8C028448
 */


/*
 * 0x8C0283AA - func_8C0283AA
 */


/*
 * 0x8C028418 - func_8C028418
 */


/*
 * 0x8C02841E - func_8C02841E
 */


/*
 * 0x8C02852A - func_8C02852A
 */


/*
 * 0x8C02861E - func_8C02861E
 */


/*
 * 0x8C028518 - func_8C028518
 */
extern const unsigned char g_vram_data[0x8C0450C0 - 0x8C212CE8]; // Approximate size based on context, adjust if known exactly. The actual data is at the address loaded into r3/r2 logic below.
// Note: In real decompilation, we would define these as extern pointers to specific tables found in global symbols or ROM maps. 
// Here we assume standard Dreamcast SDK patterns where such addresses point to function pointer arrays stored in VRAM/ROM.

extern void (*g_func_ptr_table_0x8C212CE8)(void); // Placeholder for the address 0x8C212CE8
extern unsigned char g_byte_at_offset[4];         // Buffer if needed, though direct access is used here. Actually, r3 points to a table where offset+0 holds an index or

/*
 * 0x8C028A04 - func_8C028A04 (1360 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C028A04); */

/*
 * 0x8C028F54 - func_8C028F54
 */
#include <stdint.h>

extern uint32_t _sh4_vram_8c029008; // Address stored at 0x8C029008, value is likely a pointer or offset base. Based on context of SH-4 SDK, these are often direct addresses to tables in VRAM/ROM.
extern uint32_t _sh4_vram_8c0290f4; // Address stored at 0x8C0290F4 (value: 0x8C12...) -> Actually the assembly loads a constant address into r4, then jumps to it? No.
// Correction on logic based on SH-4 SDK patterns:
// The pattern `mov.w addr,rX` followed by `.word value` usually means loading an immediate word from ROM/VRAM at 'addr' which contains another pointer or

/*
 * 0x8C02882A - func_8C02882A
 */


/*
 * 0x8C0287BC - func_8C0287BC
 */


/*
 * 0x8C029042 - func_8C029042
 */


/*
 * 0x8C02874C - func_8C02874C
 */


/*
 * 0x8C028F8E - func_8C028F8E
 */
#include <stdint.h>

extern uint32_t (*func_8C1294C8)(uint32_t, uint32_t); // 0x8C02909C -> func_8C1294C8 (likely a helper or table lookup)
// Note: The assembly uses indirect jumps via r2 and function calls. 
// Based on context of Katana SDK/MVCA2, these are likely specific engine functions loaded into the jump buffer.

extern uint32_t func_8C11E2E0(uint32_t); // 0x8C0290B8 -> func_8C11E2E0
// The table at 0x8C0290A4 contains function pointers or offsets.

extern uint32_t *g_vram_table_start; // Placeholder for the base address if not absolute, but assembly uses fixed addresses in r4/r5 context usually implying global data structures loaded once. 
// However, looking at `mov.l 0x8c0290a4,r0`, this is a direct load of an array pointer or table start relative to some segment?
// Actually, the pattern suggests:
// r1 =

/*
 * 0x8C028916 - func_8C028916
 */


/*
 * 0x8C028988 - func_8C028988
 */


/*
 * 0x8C02883C - func_8C02883C
 */


/*
 * 0x8C02925E - func_8C02925E (1798 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02925E); */

/*
 * 0x8C029A94 - func_8C029A94 (710 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C029A94); */

/*
 * 0x8C029054 - func_8C029054
 */


/*
 * 0x8C029A58 - func_8C029A58
 */
#include <stdint.h>

// Helper function declarations (extern) to ensure standalone compilation with -fsyntax-only
static void func_8C1294BC(void *a, int b); // Corresponds to address 0x8C1294BC loaded into r3
void (*func_ptr_table_entry)(int);          // Placeholder for the jump target at 0x8C0450C0

// Global data addresses (mapped from assembly constants)
static const uint32_t g_data_offset = 0x6c;       // Derived from add #116, r1 is base + offset. Wait, let's re-evaluate the math.
                                                  // Actually: mov.l addr,r3 -> jsr @r3 (call func). Then "add #116, r1". 
                                                  // The call happens at r1+0? No,

/*
 * 0x8C029E1E - func_8C029E1E
 */


/*
 * 0x8C029152 - func_8C029152
 */


/*
 * 0x8C02908E - func_8C02908E
 */


/*
 * 0x8C029964 - func_8C029964
 */


/*
 * 0x8C02999A - func_8C02999A
 */


/*
 * 0x8C029D5A - func_8C029D5A
 */


/*
 * 0x8C02A316 - func_8C02A316
 */
extern uint32_t func_8C1292D4(uint32_t arg); // Address loaded from 0x8C02A3FC/0x8C02A3F8 context, likely a helper or table lookup function. Adjust name if known globally.

// Global constants/data addresses (simulated as externs for standalone compilation)
extern const uint16_t *g_DataAddr_0; // 0x8c02a3ea -> points to base of data structure in r4/r5 context? Actually looks like an offset or specific pointer source. Let's assume it resolves to a known global array based on typical SDK patterns, but since we can't resolve the symbol name from raw bytes without more context, we treat 0x8c02a3ea as pointing to `g_DataBase`.
// Correction: In SH-4 decompilation of Katana/MVC2, these fixed addresses often point to specific global tables. 
// Let's define placeholders for the absolute memory locations if they aren't standard globals.
extern const uint16_t *const g_OffsetTable_0; // 0x8c02a3ea (likely a pointer or offset source) - Re-evaluating based on usage: `mov.w imm, rX` then used as base for `(rBase + Offset)`. 
// Actually

/*
 * 0x8C02A30A - func_8C02A30A
 */


/*
 * 0x8C029F4C - func_8C029F4C
 */


/*
 * 0x8C02A3DA - func_8C02A3DA
 */
#include <stdint.h>

// Extern declarations for referenced globals/helpers (adjust names as needed based on broader context)
extern uint32_t g_some_global_var; // Placeholder if r15 points to a specific global or stack frame base not defined here. 
                                   // In this snippet, the logic depends entirely on runtime state in registers/r6 and return value -1 vs incremented counter.

// Note: The provided assembly is 14 bytes but contains an unreachable instruction sequence after 'bf'.
// Address 0x8C02A3DA to 0x8C02A3E5 covers the logic below. 
// Instruction at 0x8C02A3E6 (mov.l @r15+, r14) is in a delay slot of an implicit return or unreachable path if 'bf' taken,
// but given standard SH-4 behavior where bf

/*
 * 0x8C02A3E8 - func_8C02A3E8
 */


/*
 * 0x8C02A41E - func_8C02A41E
 */


/*
 * 0x8C02A000 - func_8C02A000
 */


/*
 * 0x8C029F82 - func_8C029F82
 */


/*
 * 0x8C02A100 - func_8C02A100
 */


/*
 * 0x8C02A276 - func_8C02A276
 */


/*
 * 0x8C02A436 - func_8C02A436
 */


/*
 * 0x8C02A45E - func_8C02A45E
 */


/*
 * 0x8C02A454 - func_8C02A454
 */


/*
 * 0x8C02A440 - func_8C02A440
 */


/*
 * 0x8C02A424 - func_8C02A424
 */


/*
 * 0x8C02A44A - func_8C02A44A
 */


/*
 * 0x8C02A53A - func_8C02A53A (660 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02A53A); */

/*
 * 0x8C02A516 - func_8C02A516
 */


/*
 * 0x8C02A534 - func_8C02A534
 */


/*
 * 0x8C02A464 - func_8C02A464
 */
extern uint32_t func_8C02A5EC(void); // Address 0x8C02A5EC = 0x00008220, likely a pointer or value lookup table base. Adjust signature if known (e.g., extern const void* ...).
extern uint32_t func_8C149868(void); // Address 0x8C149868 from r1 at 0x8c02a5e8. Likely a table base address stored in ROM/VRAM map. Adjust signature if known.
extern uint32_t func_8C149860(void); // Address 0x8C149860 from r1 at 0x8c02a4e0. Likely a table base address stored in ROM/VRAM map. Adjust signature if known.
extern uint32_t func_8C149850(void); // Address 0x8C149850 from r1 at 0x8c02a4dc. Likely a table base address stored in ROM/VRAM map. Adjust signature if known.

// Global variables or data

/*
 * 0x8C02A7CE - func_8C02A7CE (630 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02A7CE); */

/*
 * 0x8C02AC0E - func_8C02AC0E (718 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02AC0E); */

/*
 * 0x8C02AFF0 - func_8C02AFF0 (834 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02AFF0); */

/*
 * 0x8C02B5C2 - func_8C02B5C2 (522 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02B5C2); */

/*
 * 0x8C02AA44 - func_8C02AA44
 */


/*
 * 0x8C02B7CC - func_8C02B7CC
 */


/*
 * 0x8C02AEDC - func_8C02AEDC
 */


/*
 * 0x8C02AAAE - func_8C02AAAE
 */


/*
 * 0x8C02B498 - func_8C02B498
 */


/*
 * 0x8C02B332 - func_8C02B332
 */
#include <stdint.h>

// Extern declarations for referenced functions and globals (simulated based on context)
extern void func_8C0395AC(void); // 0x8C212BF0 -> likely a helper, address resolved at runtime in asm as r3 load from table? No, wait. 
                                  // Re-evaluating addresses:
// The assembly loads pointers into registers (r3) which are then used to jump or access memory. These look like function pointer tables or data structures loaded once and reused.
// Let's define placeholders for the indirect jumps based on the literal values found in .word instructions if they represent absolute addresses, 
// OR assume standard Katana SDK patterns where these load from a table of pointers stored at those VRAM locations?
// Actually, looking closely: `mov.l 0x8c02b3ac,r13` loads an address into r13. Then later `(r0,r13)` is used as base+offset for byte access. 
// This suggests the code in .word blocks (like at B4A6) are likely data tables or function pointers stored statically, not inline jumps to absolute addresses directly unless `jsr @r` uses them.
// Wait, `mov.l 0x8c02b3ac,r13` loads a specific address into r13. 
// Let's trace: 
// B4A6 (in the .word block): mov.w ... then later used? No, the assembly provided has inline `.word` directives which are data definitions in ROM/VRAM that get loaded by `mov.l`.
// The addresses like 0x8C212BF0 at B3B4 seem to be function pointers or base addresses. 
// Since I cannot resolve external symbols without a map, and the prompt asks for C code assuming these are known entities:
extern void (*func_ptr_8c212bf0)(void); // Placeholder for 0x8C212BF0 if it's a function pointer load target? No.
// Correction on Katana SDK structure: These `.word` blocks often define lookup tables or specific constants, but here they are loaded into registers to be used as pointers immediately after `mov.l`. 
// Example at B3B4: `mov.b r15,@r11` where r11 came from a table load.
// Let's assume the addresses loaded (e.g., 0x8C212BF0) are function entry

/*
 * 0x8C02B882 - func_8C02B882 (584 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02B882); */

/*
 * 0x8C02BCA0 - func_8C02BCA0
 */
#include <stdint.h>

extern uint32_t func_8C14B0C8(void *base, int index); // Placeholder for the function at 0x8C14B0C8 if it's a call target logic not fully resolved or just data pointer usage. However, looking closely:
// The assembly loads addresses from ROM/RAM into r0 and then dereferences @(r0,r5). 
// This implies func_8C14B1F4 (at 0x8c02bd54) is likely a function address or data pointer table base.
// And func_8C14B0C8 (at 0x8c02bd58) is another one.

extern uint32_t (*func_ptr_table_entry)(void); // Generic type for the result of dereferencing if it's a function, or just

/*
 * 0x8C02BC54 - func_8C02BC54
 */


/*
 * 0x8C02BC18 - func_8C02BC18
 */


/*
 * 0x8C02BACA - func_8C02BACA
 */


/*
 * 0x8C02BD06 - func_8C02BD06
 */
extern uint32_t _SH_4_Data_Table[]; // Global data table containing function pointers at fixed offsets.

uint32_t func_8C02BD06(void) {
    return (uint32_t)_SH_4_Data_Table[(uintptr_t)(0x8c02bd64 - 0)]; 
}

/*
 * 0x8C02BD0C - func_8C02BD0C
 */


/*
 * 0x8C02BB8C - func_8C02BB8C
 */


/*
 * 0x8C02BAE0 - func_8C02BAE0
 */


/*
 * 0x8C02BCC6 - func_8C02BCC6
 */


/*
 * 0x8C02BD2E - func_8C02BD2E (2654 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02BD2E); */

/*
 * 0x8C02CAEC - func_8C02CAEC (1162 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02CAEC); */

/*
 * 0x8C02BD28 - func_8C02BD28
 */


/*
 * 0x8C02D208 - func_8C02D208 (742 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02D208); */

/*
 * 0x8C02C78C - func_8C02C78C
 */


/*
 * 0x8C02D14E - func_8C02D14E (46 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02D14E); */

/*
 * 0x8C02C924 - func_8C02C924
 */


/*
 * 0x8C02CF76 - func_8C02CF76
 */


/*
 * 0x8C02D17C - func_8C02D17C
 */


/*
 * 0x8C02D1B2 - func_8C02D1B2
 */


/*
 * 0x8C02D576 - func_8C02D576 (792 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02D576); */

/*
 * 0x8C02DAC4 - func_8C02DAC4
 */


/*
 * 0x8C02DAE0 - func_8C02DAE0
 */


/*
 * 0x8C02DBD6 - func_8C02DBD6
 */


/*
 * 0x8C02DC0E - func_8C02DC0E
 */


/*
 * 0x8C02DB6C - func_8C02DB6C (62 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02DB6C); */

/*
 * 0x8C02D88E - func_8C02D88E
 */


/*
 * 0x8C02DBAA - func_8C02DBAA
 */


/*
 * 0x8C02D4EE - func_8C02D4EE
 */


/*
 * 0x8C02D9BC - func_8C02D9BC
 */


/*
 * 0x8C02DC26 - func_8C02DC26
 */


/*
 * 0x8C02DC32 - func_8C02DC32
 */


/*
 * 0x8C02DC3E - func_8C02DC3E
 */


/*
 * 0x8C02DE94 - func_8C02DE94
 */
extern uint32_t *g_vram_base_56_r1; // Placeholder for @(56,r1) address calculation context if needed, assuming r1 holds base pointer + offset logic or is a specific register holding the VRAM block start. Based on assembly `@(56,r1)` and `@(56,r3)`, these are likely pointers to data structures in VRAM accessed via R1/R3 with an immediate 0x28 (4 bytes? No, SH-4 addressing mode is usually base+offset). Wait, standard SuperH syntax: @(disp,bn). Here disp=56. So it's loading from [R1 + 56].
extern uint32_t *g_vram_base_56_r3; // Same logic for R3

// Helper to simulate the jump table behavior if necessary, but here we just inline the jumps or assume they are

/*
 * 0x8C02DEF4 - func_8C02DEF4
 */


/*
 * 0x8C02DE12 - func_8C02DE12
 */


/*
 * 0x8C02DED6 - func_8C02DED6
 */


/*
 * 0x8C02DF20 - func_8C02DF20
 */


/*
 * 0x8C02DC4C - func_8C02DC4C
 */


/*
 * 0x8C02DCD6 - func_8C02DCD6
 */


/*
 * 0x8C02E3C8 - func_8C02E3C8
 */
extern void (*func_ptr)(void); // Helper function declared at address 0x8C02E014, assumed to take no args based on context (bsr without explicit arg list in SH-4 often implies static or specific linkage) but here it's a jump target. Assuming standard calling convention where arguments are passed if needed, though none seem used before bsr.
// Note: In Dreamcast C decompilation of Katana SDK, 'func_8C02E3CA' is the actual function name for this block or similar naming scheme might apply to 0x8c02e014. 
// However, based on strict assembly provided:

extern void (*helper_func)(void); // Placeholder for address 0x8c02e014 if it's a C function call
// Actually, looking at the pattern 'bsr addr', this is

/*
 * 0x8C02DFA6 - func_8C02DFA6
 */


/*
 * 0x8C02E2FA - func_8C02E2FA
 */


/*
 * 0x8C02E3DA - func_8C02E3DA
 */
#include <stdint.h>

// External declarations for referenced functions and globals (inferred from context)
extern uint32_t (*func_8C1294C8)(uint32_t, uint32_t); // Address 0x8C1294C8 called at r3
extern void func_8C02FDE0(void *arg);                   // Address 0x8C02FDE0 (r1)
extern void func_8C02FD26(uint32_t arg, uint32_t unused_arg); // Address 0x8C02FD26 (r1)
extern void (*func_ptr_r4)(void *arg);                  // Pointer loaded from r4 at offset E458/E45A? No, likely specific pointers. 
// Re-evaluating based on assembly:
// The

/*
 * 0x8C02E378 - func_8C02E378
 */


/*
 * 0x8C02E4CC - func_8C02E4CC
 */


/*
 * 0x8C02E454 - func_8C02E454
 */


/*
 * 0x8C02E154 - func_8C02E154
 */


/*
 * 0x8C02DFAA - func_8C02DFAA
 */


/*
 * 0x8C02E246 - func_8C02E246
 */


/*
 * 0x8C02E9DA - func_8C02E9DA (1128 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02E9DA); */

/*
 * 0x8C02EE42 - func_8C02EE42 (1680 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02EE42); */

/*
 * 0x8C02F4D2 - func_8C02F4D2 (516 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02F4D2); */

/*
 * 0x8C02F6D6 - func_8C02F6D6
 */
#include <stdint.h>

// Extern declarations for referenced globals and helpers (as per Dreamcast ABI context)
extern uint32_t g_unk_8C02F762; // Loaded into r5 initially, likely a base offset or pointer component
extern uint16_t g_unk_8C02F764; // Used in loop logic before BSR call
extern uint32_t g_unk_8C02F766; // Returned value from subroutine 0x8c03009a, used as offset incrementer? Or part of address calc. Re-evaluating based on flow: r4 gets this after add #12... wait, let's trace carefully.
// Correction on globals based on immediate analysis:
extern uint32_t g_unk_8C02F762; // mov.w

/*
 * 0x8C02E586 - func_8C02E586
 */


/*
 * 0x8C02E5E0 - func_8C02E5E0
 */


/*
 * 0x8C02E8E6 - func_8C02E8E6
 */


/*
 * 0x8C02E630 - func_8C02E630
 */


/*
 * 0x8C02E7BC - func_8C02E7BC
 */


/*
 * 0x8C02E796 - func_8C02E796
 */


/*
 * 0x8C02F6FE - func_8C02F6FE (866 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02F6FE); */

/*
 * 0x8C02FB62 - func_8C02FB62
 */


/*
 * 0x8C02FAF0 - func_8C02FAF0
 */


/*
 * 0x8C02FBE0 - func_8C02FBE0
 */
extern uint32_t func_8C02FBE6_offset[5]; // Offset array for the indirect store instruction at 0x8C02FBE4+1 (offset is in r6)
// Note: The assembly uses a relative offset from register r6. In C, we model this by accessing an external table 
// where index [r6] provides the base address or immediate adjustment if it's not a direct pointer dereference pattern.
// However, looking at `mov.b r0,@(5,r6)`: This is "Load Byte from memory at (Base + Offset)".
// Here Base = r6, Offset = 5 bytes relative to whatever r6 points to? 
// Actually, SuperH syntax @(offset, base_register) means address = [base_register] + offset.
// So it loads a byte from the absolute address: &r1[

/*
 * 0x8C02FA60 - func_8C02FA60
 */


/*
 * 0x8C02FC16 - func_8C02FC16
 */


/*
 * 0x8C02FCA2 - func_8C02FCA2
 */


/*
 * 0x8C02FB6E - func_8C02FB6E
 */


/*
 * 0x8C02FBEC - func_8C02FBEC
 */


/*
 * 0x8C02FA8A - func_8C02FA8A
 */


/*
 * 0x8C02FD26 - func_8C02FD26
 */


/*
 * 0x8C02FD62 - func_8C02FD62
 */


/*
 * 0x8C02FF52 - func_8C02FF52
 */


/*
 * 0x8C02FEC4 - func_8C02FEC4 (142 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C02FEC4); */

/*
 * 0x8C02FDB0 - func_8C02FDB0
 */


/*
 * 0x8C02FEBE - func_8C02FEBE
 */


/*
 * 0x8C03009A - func_8C03009A
 */


/*
 * 0x8C02FF5A - func_8C02FF5A
 */


/*
 * 0x8C02FDE0 - func_8C02FDE0
 */


/*
 * 0x8C02FCCC - func_8C02FCCC
 */


/*
 * 0x8C0300BA - func_8C0300BA
 */


/*
 * 0x8C0301D0 - func_8C0301D0
 */


/*
 * 0x8C030186 - func_8C030186
 */


/*
 * 0x8C0305CE - func_8C0305CE
 */


/*
 * 0x8C030626 - func_8C030626
 */


/*
 * 0x8C030106 - func_8C030106
 */


/*
 * 0x8C03068C - func_8C03068C
 */


/*
 * 0x8C0301F6 - func_8C0301F6
 */


/*
 * 0x8C0303DE - func_8C0303DE
 */


/*
 * 0x8C030452 - func_8C030452
 */
#include <stdint.h>

// External function declarations based on address analysis from assembly constants.
extern void func_8C120950(void);           // Called at 0x8C0304FC (r3) - likely a helper or callback
extern int (*func_8C1201E0)(void *);      // Called at 0x8C030500 (r2) - pointer to function taking void* returning int
extern void func_8C122710(void);           // Called at 0x8C0304FC via r3 in second block? No, addr is different. 
                                            // Re-evaluating: 0x8c030504 -> 0x8C122710
extern void func_8C122710(void);           // Called at 0x8C0304FC? No, code says mov.l #... r3 then jsr @r3. 
                                            // Let's map addresses strictly:
// 0x8c030504 -> 0x8C122710 (Called at line with add#80)
extern void func_8C122710(void);

// Helper functions identified in the assembly flow.
extern int (*func_8C123370)(void *);      // Called at 0x8c030508 (r2) - pointer to function taking void* returning int? 
                                            // Actually code does: mov.w r4, @r2 then jsr. Wait, assembly says "mov.l ... r2", "mov.w ... r4", "jsr".
// Correction on ABI usage in this specific block:
// The pattern `mov.l addr,rX`, `mov.w val,Y` (where Y is a register or immediate offset?), `jsr @addr`.
// Looking at 0x8c030510 -> 0x00008000. This looks like an interrupt vector table entry or similar constant pointer? 
// No, it's likely a function address stored in memory that gets loaded into r2/r4/etc to call via indirect jump/branch.
extern void func_8C132640(void);           // Called at 0x8c030514 (r3) - pointer arithmetic involved? 
                                            // Code: mov.l ... r3, jsr @r3. So it's a function address in memory loaded to r3 then called.

/*
 * 0x8C030AA4 - func_8C030AA4 (540 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C030AA4); */

/*
 * 0x8C0307D2 - func_8C0307D2
 */


/*
 * 0x8C0308C2 - func_8C0308C2
 */


/*
 * 0x8C030CC0 - func_8C030CC0 (60 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C030CC0); */

/*
 * 0x8C030CFC - func_8C030CFC
 */


/*
 * 0x8C030D92 - func_8C030D92
 */


/*
 * 0x8C030818 - func_8C030818
 */


/*
 * 0x8C0306DE - func_8C0306DE
 */


/*
 * 0x8C030922 - func_8C030922
 */


/*
 * 0x8C030712 - func_8C030712
 */


/*
 * 0x8C030FDC - func_8C030FDC
 */
extern uint32_t func_8C26A8E4(void); // Placeholder for the external function referenced by address 0x8C26A8E4 if it's a call, or just an extern variable depending on context. Given 'mov.l #1,r5' pattern in similar Katana SDK functions often implies setting up arguments before calling another routine at that address, but here we are writing to memory relative to r4. Let's assume 0x8C26A8E4 is a data structure or the start of an object being modified. However, looking closely: `mov.l #1,r5` was NOT in this snippet provided by user? Wait, re-reading assembly carefully.

// Correction on logic based strictly on provided bytes:
// 0x8C030FDC: mov.l    0x8c03108c,r

/*
 * 0x8C030F9E - func_8C030F9E
 */


/*
 * 0x8C030F46 - func_8C030F46
 */


/*
 * 0x8C030FAE - func_8C030FAE
 */


/*
 * 0x8C030FCC - func_8C030FCC
 */


/*
 * 0x8C030F56 - func_8C030F56
 */


/*
 * 0x8C031008 - func_8C031008
 */


/*
 * 0x8C030FFA - func_8C030FFA
 */


/*
 * 0x8C030DF0 - func_8C030DF0
 */


/*
 * 0x8C030FEC - func_8C030FEC (14 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C030FEC); */

/*
 * 0x8C031068 - func_8C031068
 */


/*
 * 0x8C031076 - func_8C031076
 */


/*
 * 0x8C031030 - func_8C031030
 */


/*
 * 0x8C031018 - func_8C031018
 */


/*
 * 0x8C03103E - func_8C03103E
 */


/*
 * 0x8C031052 - func_8C031052
 */


/*
 * 0x8C031084 - func_8C031084
 */
#include <stdint.h>
// Extern declarations for referenced symbols/globals (assumed based on context)
extern uint32_t *g_unk_ptr; // Placeholder if @(r4, 0x8C...) refers to a global table entry at offset 0x8C relative to r4 base? 
                             // Actually looking at the code: mov.l @(8,r4),r3. If r4 is an address of a struct/table, this loads from +8.
// Based on "mov.l 0x8c0310b0,r4" where [0x8C0310B0] = 0x8C26A8E4: 
extern uint32_t g_base_table_addr; // The value at address 0x8C0310B0
// Based on "mov.l @(r0,r1),

/*
 * 0x8C0310F2 - func_8C0310F2
 */


/*
 * 0x8C0310FE - func_8C0310FE
 */


/*
 * 0x8C031104 - func_8C031104
 */


/*
 * 0x8C031414 - func_8C031414 (1658 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C031414); */

/*
 * 0x8C03140C - func_8C03140C
 */


/*
 * 0x8C031410 - func_8C031410
 */


/*
 * 0x8C031A8E - func_8C031A8E
 */


/*
 * 0x8C031C22 - func_8C031C22 (724 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C031C22); */

/*
 * 0x8C031B14 - func_8C031B14
 */
#include <stdint.h>

extern uint32_t *g_8C031B46; // Address 0x8C031B46, used as stack pointer base for return address in delay slot context (though here it's just a label)
// Note: The assembly uses hardcoded addresses. We map them to extern pointers or direct casts based on usage pattern.

extern uint32_t *g_8c031b5e; // 0x8C031B46 + offset? No, let's look at the constants provided in comments.
// Actually, the assembly has absolute addresses loaded into registers r5 and r6/r7 context.
// Let's define externs for the specific memory locations referenced by the hardcoded offsets if they are globals, 
// or just use direct pointer arithmetic on the known base addresses from the disassembly notes provided

/*
 * 0x8C031BBA - func_8C031BBA
 */


/*
 * 0x8C0312D6 - func_8C0312D6
 */


/*
 * 0x8C031ACA - func_8C031ACA
 */


/*
 * 0x8C031B48 - func_8C031B48
 */


/*
 * 0x8C03221A - func_8C03221A
 */
#include <stdint.h>

// Extern declarations for referenced functions and globals based on context analysis.
extern void (*func_8C02DED6)(void); // Address 0x8C032268 -> 0x8C02DED6
extern uint16_t func_table_entry[4]; // Addresses at offsets relative to base pointers (derived from mov.w patterns)
// Note: The assembly uses hardcoded addresses for function jumps. These are mapped below based on the .word data sections provided in the prompt which act as lookup tables or direct address storage.

extern void (*func_8C1240A0)(void); // Address 0x8C032270 -> 0x8C1240A0
// The assembly contains several "mov.w" instructions loading addresses into r4/r5 which are then used in indirect jumps or calls. 
// Based on the provided .word data blocks (e.g., at 0x8c032260, etc.), these act as jump tables for specific sub-functions within a larger routine structure common in Katana SDK.
// We define

/*
 * 0x8C0320CA - func_8C0320CA
 */


/*
 * 0x8C031EF6 - func_8C031EF6
 */


/*
 * 0x8C031F34 - func_8C031F34
 */


/*
 * 0x8C032320 - func_8C032320
 */


/*
 * 0x8C032364 - func_8C032364
 */


/*
 * 0x8C0322C0 - func_8C0322C0
 */
extern uint32_t *func_8C032390(void); // Address 0x8C03552A, likely a jump table base or function pointer array entry point logic. Note: The assembly loads this address directly into r3 for indirect jumps. Given the context of SH-4 decompilation where addresses are often data pointers to code/data structures, we treat it as an external symbol representing that specific memory location which acts as a dispatcher.
extern uint8_t *g_UnknownArray; // Placeholder for global arrays accessed via offsets if not defined locally (inferred from pointer arithmetic patterns common in Katana SDK).

// Based on the assembly logic: 
// - It handles indirect jumps based on r5/r4 calculations relative to 0x8C032390.
// - If no match, it performs a linear search/swap loop starting at offset

/*
 * 0x8C031F9C - func_8C031F9C
 */


/*
 * 0x8C031F8E - func_8C031F8E
 */


/*
 * 0x8C0320CE - func_8C0320CE
 */


/*
 * 0x8C0323F8 - func_8C0323F8
 */


/*
 * 0x8C032B82 - func_8C032B82 (1022 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C032B82); */

/*
 * 0x8C032F80 - func_8C032F80
 */


/*
 * 0x8C03281C - func_8C03281C
 */


/*
 * 0x8C032FAE - func_8C032FAE
 */


/*
 * 0x8C0328EE - func_8C0328EE
 */


/*
 * 0x8C032526 - func_8C032526
 */


/*
 * 0x8C03246C - func_8C03246C
 */


/*
 * 0x8C032650 - func_8C032650
 */


/*
 * 0x8C032A28 - func_8C032A28 (346 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C032A28); */

/*
 * 0x8C033532 - func_8C033532
 */
#include <stdint.h>

extern uint32_t *g_unk_return_stack; // Placeholder for the global return stack pointer used by lds.l at r15+0x8C033536 (implied context) or local to a larger block if not external. Given "ret" behavior, this is likely returning from an interrupt handler where pr was saved on stack relative to sp/r15.

// Note: The assembly shows `lds.l @r15+,pr` followed immediately by `rts`.
// This pattern typically restores the Program Counter (PR) and Return Address Stack Pointer (RSP/AR0 in some contexts, but here PR is explicitly loaded from stack).
// However, standard SH-4 ABI uses R8-R14 as callee-saved. If this function returns via RTS after loading PR into itself or a register that holds the return address? 
//

/*
 * 0x8C03349E - func_8C03349E
 */


/*
 * 0x8C03319E - func_8C03319E
 */


/*
 * 0x8C03353C - func_8C03353C
 */
extern uint64_t func_8C0331D8(uint32_t a, uint32_t b); // Helper function referenced in BSR call at 0x8C033572
// Note: The assembly uses 'r9' as an accumulator for bit manipulation logic. 
// Based on context (dmalu.l r13,r15 followed by shifts), this likely implements a specific fixed-point or bitwise operation pattern common in Katana SDK math routines, where the result is accumulated into R0/R2 and then shifted back out to form bytes/words before being stored via movmu.
// The exact semantics of func_8C0331D8 depend on its definition elsewhere; here we assume it returns a uint64_t based on dmulu usage context or acts as the core calculation unit returning in R0/R2 pair if not passed args differently

/*
 * 0x8C0334A8 - func_8C0334A8
 */


/*
 * 0x8C0335B0 - func_8C0335B0
 */


/*
 * 0x8C0335A6 - func_8C0335A6
 */


/*
 * 0x8C0333E2 - func_8C0333E2
 */


/*
 * 0x8C03329C - func_8C03329C
 */


/*
 * 0x8C0331D0 - func_8C0331D0
 */


/*
 * 0x8C03379C - func_8C03379C
 */
#include <stdint.h>

// Extern declarations for referenced global variables/functions found in the disassembly context.
extern uint32_t g_unk_8c1f9d90; // Address 0x8C1F9D90 (from 0x8c0338b4)
extern uint32_t g_unk_8c1f9d80; // Address 0x8C1F9D80 (from 0x8c0338ac)
extern uint32_t g_unk_8c1f9d8c; // Address 0x8C1F9D8C (from 0x8c0338b0)
extern uint32_t g_unk_8c1f9d94; // Address 0x8C1F9D94 (from 0x8c0338b8)
extern uint32_t g_unk_8c1f9d98; // Address 0x8C1F9D98 (from 0x8c0338bc)
extern void (*g_func_ptr_8c1f9fc)(void);   // Function pointer at 0x8CE60FC? No

/*
 * 0x8C0335E2 - func_8C0335E2
 */


/*
 * 0x8C033622 - func_8C033622
 */


/*
 * 0x8C03387E - func_8C03387E (588 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C03387E); */

/*
 * 0x8C03362C - func_8C03362C
 */


/*
 * 0x8C033718 - func_8C033718
 */


/*
 * 0x8C033ACA - func_8C033ACA
 */


/*
 * 0x8C0335DC - func_8C0335DC
 */


/*
 * 0x8C033656 - func_8C033656
 */


/*
 * 0x8C03385A - func_8C03385A (36 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C03385A); */

/*
 * 0x8C033B0A - func_8C033B0A (622 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C033B0A); */

/*
 * 0x8C0344CA - func_8C0344CA (1022 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C0344CA); */

/*
 * 0x8C0348C8 - func_8C0348C8 (802 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C0348C8); */

/*
 * 0x8C033FD8 - func_8C033FD8
 */


/*
 * 0x8C033F6C - func_8C033F6C
 */


/*
 * 0x8C033FDC - func_8C033FDC
 */


/*
 * 0x8C034BEA - func_8C034BEA
 */


/*
 * 0x8C03415A - func_8C03415A
 */


/*
 * 0x8C033D78 - func_8C033D78
 */


/*
 * 0x8C0342DA - func_8C0342DA (496 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C0342DA); */

/*
 * 0x8C034F3A - func_8C034F3A
 */
#include <stdint.h>

// External declarations for symbols referenced in the function (based on Dreamcast ABI and context)
extern uint16_t (*func_8C1294C8)(void); // Address loaded from 0x8C03507C/0x8C0350CC, likely a callback or table lookup

// Global variables inferred from constant loads (addresses relative to base)
extern uint8_t g_vram_base_6a;   // Offset for r4 + 20 -> used as index/base? Actually looks like an offset into VRAM. 
                                  // Based on `mov.w #20,r0` then `@(r0,r4)` where r4 is base, this acts as a stride or specific address calculation.
// Re-evaluating constants:
extern uint8_t g_vram_offset_6a;  // Value at 0x8C03506A (likely used to compute an offset)
extern uint16_t (*g_callback_ptr)(void); // Pointer loaded from 0x8C03507C

// Helper macros for SH-4 specific operations if not available in target C, though standard shifts work.
#define EXTU_B(x) ((uint32_t)((x)) & 0xFFu)
#define EXTU_W(x) ((uint64_t)(

/*
 * 0x8C034E70 - func_8C034E70
 */


/*
 * 0x8C034CD6 - func_8C034CD6
 */


/*
 * 0x8C03515C - func_8C03515C (658 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C03515C); */

/*
 * 0x8C035118 - func_8C035118
 */


/*
 * 0x8C034BFA - func_8C034BFA
 */


/*
 * 0x8C034C18 - func_8C034C18
 */


/*
 * 0x8C034DEE - func_8C034DEE
 */


/*
 * 0x8C034D7E - func_8C034D7E
 */


/*
 * 0x8C035000 - func_8C035000
 */
#include <stdint.h>

// Global variables referenced in the function (VRAM addresses/constants)
extern uint32_t g_vram_0x8C010524; // Base address for r7 logic? Or constant pool base. Based on usage at 0x8c035072, it seems to be a pointer or offset source. Let's assume it holds the value loaded into r7 initially if not an immediate. Actually, looking at `mov.w 0x8C010524,r7` in previous context usually implies loading from memory unless it's absolute addressing which SH-4 doesn't do for immediates >32k directly without specific instructions. Wait, the assembly says `mov.w #...`. No, `mov.w imm` is not standard SH-4 immediate load to register if large? 
// Correction: The provided asm uses `mov.w 0x8c035072,r7`. This loads a word from memory address 0x8C035072.
extern uint16_t g_vram_0x8C035072; // Address holding the value for r7 (likely an offset or pointer)
extern uint16_t g_vram_0x8C035074; // Base address used in many loads `@(r0,r4)` where r0 is loaded from here. 
// Wait, looking at 0x8c035072: it's a memory location containing the value to load into R7?
// Let's re-read carefully: `mov.w 0x8C035072,r7`. This loads [0x8C035072] -> r7. 
// So g_vram_0x8C035072 is the address of that word.
extern uint16_t *g_ptr_base; // Placeholder for

/*
 * 0x8C0355F2 - func_8C0355F2
 */
extern void *func_8C035648(void); // Address: 0x8C1291DC, likely a function pointer lookup table entry
extern unsigned int func_8C03564C; // Value: 0xFFFFFFFF (Mask)
extern void (*(*table_ptr)(void))(int); // Pointer to the actual callback or logic handler at offset +8

// Global/Context pointers assumed based on SH-4 SDK patterns where r4 is often a context struct pointer.
// Assuming standard Katana SDK structure layout: [0] = some data, [4] = flag/count, [12] = next ptr? 
// Based on usage: @(4,r4) -> check if valid; @(8,r4) -> store result back to offset 8 of the struct.
extern unsigned int *context_struct_ptr_r4; // Placeholder for r

/*
 * 0x8C0355A0 - func_8C0355A0
 */


/*
 * 0x8C03544C - func_8C03544C
 */


/*
 * 0x8C0355C2 - func_8C0355C2
 */


/*
 * 0x8C03563A - func_8C03563A (6 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C03563A); */

/*
 * 0x8C035640 - func_8C035640
 */


/*
 * 0x8C03552A - func_8C03552A
 */


/*
 * 0x8C03580C - func_8C03580C
 */


/*
 * 0x8C0353EE - func_8C0353EE
 */


/*
 * 0x8C0354B2 - func_8C0354B2
 */


/*
 * 0x8C035964 - func_8C035964
 */


/*
 * 0x8C03593E - func_8C03593E
 */


/*
 * 0x8C03594A - func_8C03594A
 */


/*
 * 0x8C035B2E - func_8C035B2E (590 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C035B2E); */

/*
 * 0x8C035938 - func_8C035938
 */


/*
 * 0x8C035E6E - func_8C035E6E (724 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C035E6E); */

/*
 * 0x8C03589A - func_8C03589A
 */


/*
 * 0x8C035D7C - func_8C035D7C
 */


/*
 * 0x8C03596A - func_8C03596A
 */


/*
 * 0x8C035A5A - func_8C035A5A
 */
#include <stdint.h>

// External function declarations inferred from usage at addresses 0x8C035A6E, etc.
extern uint32_t (*func_8C14D1E0)(void); // Called via jsr @r3 where r3 points to this address (from A72)
// Note: The assembly uses indirect jumps heavily with addresses loaded from data sections.
// Based on the pattern `mov.l 0x8c..., rX` followed by `jsr @rY`, these are function pointers or VTables.

extern uint32_t (*func_8C14D1E0_ptr)(void); // Alias for clarity if needed, but direct usage below
// Re-evaluating based on specific addresses in assembly:
// A72 loads 0x8c035b90 (which is 0x8C14D1E0) into r1. Then later uses it? No, let's trace carefully.

extern uint32_t (*func_8C26823C)(void); // Loaded at A6C
// Actually, looking at the assembly:
// 0x8c035b94 = 0x8C11E730 -> func called at A8A (jsr @r2)
extern uint32_t (*func_8C11E730)(void);

// 0x8

/*
 * 0x8C0362B0 - func_8C0362B0 (628 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C0362B0); */

/*
 * 0x8C036682 - func_8C036682
 */
extern void func_8C036682(void); // Placeholder for external declaration if needed, though this is a stub.

void func_8C036682() {
    return; 
}

/*
 * 0x8C036142 - func_8C036142
 */


/*
 * 0x8C036238 - func_8C036238
 */


/*
 * 0x8C036266 - func_8C036266
 */


/*
 * 0x8C0366F4 - func_8C0366F4
 */
extern uint32_t func_8C042D8C(uint32_t a, uint32_t b); // Placeholder for jsr @r3 (addr 0x8C042D8C)
extern void func_8C213046(void *a);                      // Placeholder for jsr @r3 (addr 0x8C213046, r2 points to it after mov.b check? No, logic is complex. Let's trace carefully.)

// Re-evaluating the indirect jumps based on static data at fixed addresses:
// The code loads pointers from ROM/data segments into registers and then indirection happens.
// Since I cannot see the content of 0x8C213046 or similar without memory dump, 
// but the prompt implies these are function calls via indirect jump tables often found in SDK

/*
 * 0x8C03673E - func_8C03673E
 */


/*
 * 0x8C036150 - func_8C036150
 */


/*
 * 0x8C036524 - func_8C036524
 */


/*
 * 0x8C036686 - func_8C036686
 */


/*
 * 0x8C036AB0 - func_8C036AB0 (520 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C036AB0); */

/*
 * 0x8C036F50 - func_8C036F50
 */
#include <stdint.h>

// Extern declarations for global variables referenced by absolute addresses in the assembly.
extern uint8_t g_vram_0x8C26823C[1]; // Address 0x8C26823C (derived from mov.l at F7C)
extern const struct { 
    int field_offset; // Offset for byte write: r4 + offset -> target address? No, logic below suggests indirect.
} g_data_table_0x8c036f78 = {{ 92 }}; // Address 0x8c036f78 contains the value 92 used as an index/offset

// Reconstructed global based on assembly flow: 
// The code loads a pointer from 0x8C010524 (implied by context of Katana SDK, but here we see

/*
 * 0x8C036CB8 - func_8C036CB8
 */


/*
 * 0x8C036DDE - func_8C036DDE
 */


/*
 * 0x8C0370A4 - func_8C0370A4
 */
extern void func_8C0370CE(void);
// Global data addresses (derived from assembly constants)
static const uint32_t g_data_offset_table[] = { 16, 24 }; // Derived from .word sequences at offsets relative to base logic if needed, but here used as immediate loads. Actually looking closely: 
// The code does `mova` then `.word`. This is a jump table or switch dispatch mechanism embedded in the assembly that gets resolved by loading an offset into r0 and using it for branching/offsetting.
// Let's re-analyze the specific logic at 8C03712A-8C03714E which seems to be a large switch statement on input `r0` (value from memory).

extern uint32_t g_unk_8C26823C; // Address loaded into r6
static const uint32_t *g_ptr_table = (uint32_t *)(uintptr_t)0x8C212CE8; // Loaded at 0x8c03714e logic? No, let's trace carefully.

// Re-evaluating the assembly structure for C representation:
// The function initializes a buffer starting at r14 (offset by some base).
// It writes specific bytes to fixed offsets in this buffer based on input conditions.
// Then it performs a lookup into `g_unk_8C26823

/*
 * 0x8C037176 - func_8C037176
 */


/*
 * 0x8C036E92 - func_8C036E92
 */


/*
 * 0x8C036CC6 - func_8C036CC6
 */


/*
 * 0x8C0368F4 - func_8C0368F4
 */


/*
 * 0x8C036F76 - func_8C036F76
 */


/*
 * 0x8C037A46 - func_8C037A46
 */
#include <stdint.h>

// Extern declarations for referenced addresses and functions found in the disassembly context.
extern uint32_t (*func_8C037BA0)(void); // Address 0x8C037B94 points to this or similar logic, but direct call uses mov.a which implies a function pointer load at runtime offset? Actually looking closely: 
// The assembly loads addresses into r0 via .word instructions. These are likely pointers to functions stored in the ROM/VRAM map table.
extern uint32_t (*func_8C14D310)(void); // 0x8c037b90 -> This is a function pointer? No, usually data tables point to code or structs. 
// Re-evaluating based on SH-4 Katana SDK patterns:
// The .word instructions at offsets like A6C (A8), etc., are loading addresses of functions from the "Jump Table" in VRAM/ROM.
// Let's define placeholders for these external function pointers as they appear to be loaded dynamically or statically mapped.

extern uint32_t (*func_0x8c14d310)(void); // Placeholder for 0x8C14D310 if it is a func ptr, but usually .word loads an address directly into rX which might then be used as a pointer or index.
// Correction: In Katana SDK, these addresses (like 0x8c037ba0) are often function pointers stored in the ROM map table loaded at runtime. 
// However, without seeing the loader code that populates `r4` with this specific address

/*
 * 0x8C0379BC - func_8C0379BC
 */


/*
 * 0x8C037B8C - func_8C037B8C (840 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C037B8C); */

/*
 * 0x8C037360 - func_8C037360
 */


/*
 * 0x8C037B36 - func_8C037B36
 */
#include <stdint.h>

// Extern declarations for referenced addresses and functions
extern uint32_t *g_8C0395C6; // Address stored at 0x8C037BBC (derived from context)
extern void (*func_ptr)(void); // Placeholder for the function called via JSR @r11

// Note: The actual addresses are embedded in VRAM/ROM. 
// In a real decompiler, these would be resolved to their final linked values or kept as constants if static analysis allows.
#define ADDR_8C037BAC  (uint32_t)0x8C136694 // Base address for the structure being iterated over? Or just an offset base. 
// Based on usage: r3 = mov.l [addr], then shll, implies shifting a pointer or index.
#define ADDR_8

/*
 * 0x8C037ED4 - func_8C037ED4
 */


/*
 * 0x8C037594 - func_8C037594
 */


/*
 * 0x8C037864 - func_8C037864
 */


/*
 * 0x8C0373D2 - func_8C0373D2
 */


/*
 * 0x8C0376BE - func_8C0376BE
 */


/*
 * 0x8C038098 - func_8C038098 (836 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C038098); */

/*
 * 0x8C0383DC - func_8C0383DC (646 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C0383DC); */

/*
 * 0x8C038662 - func_8C038662 (678 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C038662); */

/*
 * 0x8C038ADA - func_8C038ADA (656 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C038ADA); */

/*
 * 0x8C038094 - func_8C038094
 */


/*
 * 0x8C037FC0 - func_8C037FC0
 */


/*
 * 0x8C038908 - func_8C038908
 */


/*
 * 0x8C038D6A - func_8C038D6A
 */


/*
 * 0x8C038E6A - func_8C038E6A
 */


/*
 * 0x8C038E9C - func_8C038E9C
 */


/*
 * 0x8C039250 - func_8C039250
 */


/*
 * 0x8C039240 - func_8C039240
 */


/*
 * 0x8C03943A - func_8C03943A
 */
#include <stdint.h>

// Extern declarations for referenced addresses and functions found in the assembly trace.
extern uint32_t (*func_8C0395C6)(uint32_t, int); // Address 0x8C0395C6 loaded into r3 at various points
extern void func_8C14D4A0(uint32_t *dest, const uint32_t *src, uint32_t count); // Address 0x8C14D4A0 (shll logic) - inferred from usage pattern or similar helper if not a direct call. Actually looking at the code: `jsr @r3` where r3 is set to an address that performs shifts? No, let's re-examine specific calls.
// Re-evaluating indirect jumps based on context usually implies standard library functions like memcpy/memset or custom helpers in Katana SDK.
// 0x8C14D4A0: Used with `shll2` logic before it? Wait, the assembly at 0x8C039538 loads r2 = 0x8C14D4A0 then does shll2 on r3. This looks like a specific helper or just data movement if misread. 
// Let's look closer: `mov.l #7,r3` ... `jsr @r3`. If r3 is an address of a function, it calls that function.
// The assembly at 0x8C1

/*
 * 0x8C039588 - func_8C039588
 */


/*
 * 0x8C039514 - func_8C039514
 */


/*
 * 0x8C0390EC - func_8C0390EC
 */


/*
 * 0x8C0391D6 - func_8C0391D6
 */


/*
 * 0x8C038F34 - func_8C038F34
 */


/*
 * 0x8C0395AC - func_8C0395AC (26 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C0395AC); */

/*
 * 0x8C039260 - func_8C039260
 */


/*
 * 0x8C0399C0 - func_8C0399C0
 */
#include <stdint.h>

extern uint32_t *g_8C26886C; // Address loaded at 0x8C039A5C
extern const struct { /* Struct definition inferred from context */ } g_DataBlock1; 
// Note: The code accesses offsets relative to base addresses. Assuming standard SDK layout where these globals point to the start of data structures containing packed structs or arrays accessed via pointer arithmetic and bit shifts (common in Katana engine for hitboxes/physics).
extern const uint32_t *g_8C26823C; // Address loaded at 0x8C039A54
// The logic suggests a lookup table or specific data structure traversal. 
// Given the bit shifts (shll, shll2) and memory access patterns (@(r0,r3)), this is likely unpacking packed integer fields from

/*
 * 0x8C039686 - func_8C039686
 */


/*
 * 0x8C0395C6 - func_8C0395C6
 */


/*
 * 0x8C039C64 - func_8C039C64 (732 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C039C64); */

/*
 * 0x8C039632 - func_8C039632
 */


/*
 * 0x8C039BAA - func_8C039BAA
 */


/*
 * 0x8C0396A0 - func_8C0396A0
 */


/*
 * 0x8C03973A - func_8C03973A
 */


/*
 * 0x8C0398D2 - func_8C0398D2
 */


/*
 * 0x8C039A16 - func_8C039A16 (404 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C039A16); */

/*
 * 0x8C039F40 - func_8C039F40 (570 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C039F40); */

/*
 * 0x8C03A17A - func_8C03A17A (662 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C03A17A); */

/*
 * 0x8C03A410 - func_8C03A410 (894 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C03A410); */

/*
 * 0x8C03AA2C - func_8C03AA2C (526 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C03AA2C); */

/*
 * 0x8C03ACB8 - func_8C03ACB8 (1038 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C03ACB8); */

/*
 * 0x8C03AC3A - func_8C03AC3A
 */
#include <stdint.h>

// Extern declarations for referenced functions and data (as per Dreamcast ABI context)
extern uint32_t (*func_8C01F5E0)(void); // Corresponds to bsr 0x8c03aa90 based on typical SDK patterns, adjusted if needed. 
    // Note: The assembly uses absolute addresses for the BSR targets which are likely function pointers stored in ROM/RAM constants.
    // Without a symbol table mapping these specific hex values (0x8C268340 etc) to symbols, we assume they resolve to functions returning uint32_t or void*.
    // Given 'tst r0' checks for zero return value of the BSR'd function:
extern int (*func_8C1A9E50)(void); // Placeholder name mapping 0x8c03aa

/*
 * 0x8C03A97E - func_8C03A97E
 */


/*
 * 0x8C03A78E - func_8C03A78E
 */


/*
 * 0x8C03AC66 - func_8C03AC66
 */


/*
 * 0x8C03AC9E - func_8C03AC9E
 */


/*
 * 0x8C03B304 - func_8C03B304 (672 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C03B304); */

/*
 * 0x8C03B154 - func_8C03B154
 */


/*
 * 0x8C03B0C6 - func_8C03B0C6
 */


/*
 * 0x8C03B266 - func_8C03B266
 */


/*
 * 0x8C03BA36 - func_8C03BA36
 */


/*
 * 0x8C03B996 - func_8C03B996
 */


/*
 * 0x8C03B85E - func_8C03B85E
 */


/*
 * 0x8C03B16A - func_8C03B16A
 */


/*
 * 0x8C03B5A4 - func_8C03B5A4
 */


/*
 * 0x8C03B78C - func_8C03B78C (210 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C03B78C); */

/*
 * 0x8C03BA96 - func_8C03BA96
 */


/*
 * 0x8C03BA3C - func_8C03BA3C
 */


/*
 * 0x8C03BA9A - func_8C03BA9A
 */


/*
 * 0x8C03BA4A - func_8C03BA4A
 */


/*
 * 0x8C03BB6E - func_8C03BB6E
 */
#include <stdint.h>

extern void func_8C010524(uint32_t *); // Address 0x8C010524 from r3 load at BB72
extern uint32_t (*func_ptr_table)(uint32_t, int) __attribute__((section(".data"))); 
// Note: The actual function pointer table is loaded dynamically or statically. Based on context of SH-4 decompilation for Dreamcast SDKs, these are often global pointers stored in VRAM/ROM data sections accessed via absolute loads if not defined as extern globals here. However, the assembly uses `mov.l 0x8c03bc58,r3` which points to a function address (0x8C030F24). We must assume this is an external symbol or resolve it from data.
// Given the constraint "Do NOT write long

/*
 * 0x8C03BBC0 - func_8C03BBC0
 */


/*
 * 0x8C03BBE2 - func_8C03BBE2
 */


/*
 * 0x8C03BB3E - func_8C03BB3E
 */


/*
 * 0x8C03BC3E - func_8C03BC3E
 */


/*
 * 0x8C03BAD8 - func_8C03BAD8
 */


/*
 * 0x8C03BE7A - func_8C03BE7A
 */
extern uint32_t func_8C03BBE2(uint32_t arg); // Placeholder for the actual function name at 0x8C03BBE2
extern void *g_ptr_table_start;              // Corresponds to address stored in r4 (0x8C26823C)
extern uint16_t g_counter_offset_1;         // Byte offset logic, derived from context if needed
extern uint16_t (*func_jump_target)(void);  // Function pointer table at 0x8C0F71A8

// Global constants embedded in the binary (addresses of data)
static const uintptr_t DATA_ADDR_A = 0x8C26823CU; 
static const uint32_t OFFSET_B_4 = 0x8C0F71A8U - 0xCUL; // Derived from

/*
 * 0x8C03BE0A - func_8C03BE0A
 */


/*
 * 0x8C03BD24 - func_8C03BD24
 */


/*
 * 0x8C03BF94 - func_8C03BF94 (720 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C03BF94); */

/*
 * 0x8C03BE26 - func_8C03BE26
 */


/*
 * 0x8C03BE48 - func_8C03BE48
 */


/*
 * 0x8C03BEB2 - func_8C03BEB2
 */
#include <stdint.h>

extern uint32_t _gbr; // Global Base Register (Dreamcast ABI)
// Note: The following external symbols are inferred from the absolute addresses used in jumps/calls.
// In a real decompiler context, these would be resolved to actual function names or data structures.
extern void (*func_8c03bbe2)(void); // Address 0x8C03BBE2 (BSR target)
extern uint16_t *data_ptr_r9;       // Derived from address 0x8C03BEC4 + offset logic, likely a table lookup base. 
                                     // Specifically: mov.w 0x8c03b17e -> bra ... then load @r9? No, r9 is loaded at BECO.
// Re-evaluating the flow based on standard Katana SDK patterns for state machines

/*
 * 0x8C03BF10 - func_8C03BF10
 */


/*
 * 0x8C03BD56 - func_8C03BD56
 */


/*
 * 0x8C03BC76 - func_8C03BC76
 */


/*
 * 0x8C03C4C0 - func_8C03C4C0
 */


/*
 * 0x8C03C4E2 - func_8C03C4E2
 */


/*
 * 0x8C03C5D2 - func_8C03C5D2
 */
#include <stdint.h>

// Extern declarations for referenced symbols (as per Dreamcast ABI and context)
extern uint16_t *gbr_27; // @(27,gbr) -> 0x8C26... likely a GBR-relative offset table or similar structure access pattern, but treated as extern pointer base if direct. However, the asm uses `@(offset, gbr)` which is specific to SH-4's relative addressing from Global Base Register (GCR/GPR). In standard C without knowing exact linker map, we assume these are resolved at link time or represent offsets into a known structure. Given the instruction pattern `(27,gbr)`, it implies `gcr + 0x8C26`? No, SH-4 uses GBR for relative addressing in some modes (like DSP), but standard mode is Rn+disp. Wait, looking at `mov.b @(3,r3)` vs `@(27,gbr)`. The syntax `(offset, register)` usually implies the offset from that specific base if it's a special instruction or pseudo-op generated by an assembler targeting SH-4 with GBR support (common in Sega SDKs for fast access

/*
 * 0x8C03C560 - func_8C03C560
 */


/*
 * 0x8C03C67A - func_8C03C67A
 */


/*
 * 0x8C03C692 - func_8C03C692
 */


/*
 * 0x8C03C52C - func_8C03C52C
 */


/*
 * 0x8C03C440 - func_8C03C440
 */


/*
 * 0x8C03C6AA - func_8C03C6AA
 */
extern uint8_t *g_vram_base; // Assumed global VRAM base pointer based on context of SH-4 Dreamcast decompilation patterns for such small stubs. If not present, this function is a raw jump table entry and may require linker resolution or specific runtime data access outside standard C scope without the actual address definition.

// Note: This assembly block contains multiple .word instructions which are typically Jump/Call targets in SH-4 ROM/Virtual Memory maps when used as small stubs with immediate offsets, OR it is a jump table where r0 holds an index and subsequent words hold addresses to be jumped to via indirect addressing (e.g., mov @(r0), pc).
// Given the pattern: load offset -> word address -> loop or stack manipulation? 
// Actually, looking at SH-4 calling conventions for small functions in Katana SDK:
// This looks like a jump table where r0

/*
 * 0x8C03C264 - func_8C03C264
 */


/*
 * 0x8C03C7D4 - func_8C03C7D4
 */
#include <stdint.h>

extern void func_8C010524(int, int); // Placeholder for address at offset 0xf446 relative to r4+? or absolute logic based on context. Assuming standard SDK pattern where .word acts as a jump target encoded with immediate args if not direct call.
// Note: The assembly uses "mov #imm" followed by ".word addr". In SH-4, this is often an indirect branch setup or specific inline expansion for function calls/jumps found in Katana SDK's custom calling conventions where the address word encodes parameters and target. 
// Given the pattern `mov #32,r0` then `.word 0xf446`, it implies a call to an absolute address with argument preparation, OR more likely in this specific SH-4 inline expansion style:
// The ".word" instruction here is actually encoding a jump/branch

/*
 * 0x8C03C6EC - func_8C03C6EC
 */


/*
 * 0x8C03C6C0 - func_8C03C6C0
 */


/*
 * 0x8C03C6D6 - func_8C03C6D6
 */


/*
 * 0x8C03C712 - func_8C03C712
 */


/*
 * 0x8C03C832 - func_8C03C832
 */


/*
 * 0x8C03C75E - func_8C03C75E
 */


/*
 * 0x8C03C738 - func_8C03C738
 */


/*
 * 0x8C03C870 - func_8C03C870
 */


/*
 * 0x8C03C7F6 - func_8C03C7F6 (60 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C03C7F6); */

/*
 * 0x8C03CB58 - func_8C03CB58
 */
extern uint16_t (*func_0f318)(void); // Placeholder for external function at address 0xF318
extern void func_0xf427(void);       // Placeholder for external function at address 0xF427 (r4+arg)

// Note: The assembly provided contains multiple jump targets and data structures that appear to be 
// part of a larger control flow or lookup table, but the snippet itself is fragmented.
// Based on standard SH-4 Dreamcast patterns for Marvel vs Capcom 2 SDKs:
// - r1 = base offset (likely derived from argument in r4) + constant
// - The .word instructions at offsets are likely jump targets stored as absolute addresses or relative jumps.
// Given the context of "func_8C03CB58" being a small stub, this looks like it might be 
// part

/*
 * 0x8C03C8F6 - func_8C03C8F6
 */


/*
 * 0x8C03C952 - func_8C03C952
 */


/*
 * 0x8C03CA24 - func_8C03CA24
 */


/*
 * 0x8C03CB92 - func_8C03CB92
 */


/*
 * 0x8C03CBE8 - func_8C03CBE8
 */


/*
 * 0x8C03CA76 - func_8C03CA76
 */


/*
 * 0x8C03CB04 - func_8C03CB04
 */


/*
 * 0x8C03C9AE - func_8C03C9AE
 */


/*
 * 0x8C03CB78 - func_8C03CB78
 */
extern uint32_t func_8C010524(void); // Placeholder for the function at r4 address if it's a call, but here r4 is used as pointer. Assuming global data or similar based on context of Katana SDK often using globals in registers directly passed by caller logic not shown. However, strictly following ABI: arguments are r3,r5 etc? Wait, Dreamcast C calling convention usually passes args differently than SH-4 inline asm sometimes seen in low level tools like Katana which might use specific register sets or this is a leaf function where inputs come from stack/registers defined by caller context not standard SWABI. 
// Re-evaluating ABI for Katana SDK / Marvel vs Capcom 2:
// The prompt says "Arguments in r4, r5, r6, r7". This implies the CALLER puts args there? Or is this a specific toolchain convention

/*
 * 0x8C03CF6A - func_8C03CF6A (688 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C03CF6A); */

/*
 * 0x8C03D290 - func_8C03D290
 */


/*
 * 0x8C03D466 - func_8C03D466 (4 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C03D466); */

/*
 * 0x8C03D21A - func_8C03D21A
 */


/*
 * 0x8C03D4B2 - func_8C03D4B2 (638 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C03D4B2); */

/*
 * 0x8C03D46E - func_8C03D46E
 */


/*
 * 0x8C03D312 - func_8C03D312
 */


/*
 * 0x8C03CDFE - func_8C03CDFE
 */


/*
 * 0x8C03CC1C - func_8C03CC1C
 */


/*
 * 0x8C03D46A - func_8C03D46A
 */


/*
 * 0x8C03D952 - func_8C03D952 (724 bytes) - STUBBED FOR REVIEW
 */
/* INCLUDE_ASM("asm/sys_init.s", func_8C03D952); */
