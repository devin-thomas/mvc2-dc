#include "types.h"
#include "sh4.h"

/*
 * Dreamcast CRT0 / Katana SDK Lib Handle Structure
 * Matches binary offsets 0x8C010030 - 0x8C010080
 */

typedef struct {
    char name[16];
    u32  flags;
    u32  version;
    void *handler;
    u32  reserved;
} LibHandleEntry;

/* Lib Handle Table markers referenced by Shinobi runtime */
const char g_lib_handle_start_str[] = "Lib Handle Start";
const char g_lib_handle_end_str[]   = "Lib Handle End  ";

void start(void) {
    /* Handled by asm/crt0.s NOP slide and hardware bootstrap */
}
