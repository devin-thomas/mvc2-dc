#ifndef MVC2_INPUT_H
#define MVC2_INPUT_H

#include "../types.h"

/*
 * Controller Input Bitmasks (Dreamcast Controller / Arcade Layout)
 *
 * Arcade 6-button button layout:
 *   LP (Light Punch)  | HP (Heavy Punch) | A1 (Assist 1)
 *   LK (Light Kick)   | HK (Heavy Kick)  | A2 (Assist 2)
 */

#define BTN_UP          (1 << 0)
#define BTN_DOWN        (1 << 1)
#define BTN_LEFT        (1 << 2)
#define BTN_RIGHT       (1 << 3)
#define BTN_LP          (1 << 4)    /* X button */
#define BTN_LK          (1 << 5)    /* A button */
#define BTN_HP          (1 << 6)    /* Y button */
#define BTN_HK          (1 << 7)    /* B button */
#define BTN_A1          (1 << 8)    /* L trigger */
#define BTN_A2          (1 << 9)    /* R trigger */
#define BTN_START       (1 << 10)

typedef struct {
    u16 raw_buttons;
    u16 pressed;
    u16 released;
    u8  stick_dir;
} MvC2InputState;

/*
 * Low-level controller state matching binary offsets 0x8C010080 - 0x8C010324
 * Stored in global controller buffer at 0x8C2681DC.
 */
typedef struct {
    u16 current_buttons;   /* 0x00: Held raw buttons */
    u16 previous_buttons;  /* 0x02: Buttons from previous frame */
    u16 pressed;           /* 0x04: Newly pressed buttons this frame */
    u16 released;          /* 0x06: Newly released buttons this frame */
    u16 analog_x;          /* 0x08: Joystick horizontal */
    u16 analog_y;          /* 0x0A: Joystick vertical */
    u16 trigger_l;         /* 0x0C: Left analog trigger (0-255) */
    u16 trigger_r;         /* 0x0E: Right analog trigger (0-255) */
    u16 repeat_buttons;    /* 0x10: Buttons firing in auto-repeat mode */
    u16 repeat_counter;    /* 0x12: Debounce and auto-repeat timer */
} MvC2PadState;


#endif /* MVC2_INPUT_H */

