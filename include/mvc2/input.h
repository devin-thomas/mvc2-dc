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

#endif /* MVC2_INPUT_H */
