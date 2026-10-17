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
