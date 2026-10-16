#ifndef MVC2_OBJECT_H
#define MVC2_OBJECT_H

#include "../types.h"

/*
 * Capcom Fighting Engine Object Struct (MvC2 / CPS2 / NAOMI / Dreamcast)
 *
 * Core offsets verified across SFZ3/SFA3 and MvC2 ports:
 *   +0x00: Object active flags / state
 *   +0x0B: Facing direction (1 = right, -1 / 0 = left)
 *   +0x12: World coordinate X (s16)
 *   +0x16: World coordinate Y (s16)
 *   +0x1C: Animation data pointer (const void *)
 *   +0x3A: Animation timer / frame countdown (u16)
 */

typedef struct {
    s16 left;
    s16 top;
    s16 right;
    s16 bottom;
} MvC2Box;

typedef struct MvC2Object {
    /* 0x00 */ u8  active_flags;
    /* 0x01 */ u8  object_type;
    /* 0x02 */ u16 routine_index;
    /* 0x04 */ u32 status_flags;
    /* 0x08 */ u8  player_index;
    /* 0x09 */ u8  team_slot;       /* Point, Assist 1, Assist 2 */
    /* 0x0A */ u8  costume_color;   /* Button selection 0-5 */
    /* 0x0B */ s8  facing_dir;      /* 1 = facing right, -1 = left */
    /* 0x0C */ s16 vel_x;
    /* 0x0E */ s16 vel_y;
    /* 0x10 */ s16 accel_x;
    /* 0x12 */ s16 pos_x;           /* World X */
    /* 0x14 */ s16 accel_y;
    /* 0x16 */ s16 pos_y;           /* World Y */
    /* 0x18 */ u32 state_timer;
    /* 0x1C */ const void *anim_ptr;/* Current animation sequence pointer */
    /* 0x20 */ u16 anim_frame;
    /* 0x22 */ u16 current_move_id;
    /* 0x24 */ u32 hit_flags;
    /* 0x28 */ s16 health;
    /* 0x2A */ s16 red_health;      /* Recoverable assist health */
    /* 0x2C */ u16 super_meter;
    /* 0x2E */ u16 hit_stun_timer;
    /* 0x30 */ u16 block_stun_timer;
    /* 0x32 */ u16 unk_32;
    /* 0x34 */ u32 palette_ptr;
    /* 0x38 */ u16 sprite_index;
    /* 0x3A */ u16 anim_timer;      /* Animation frame countdown timer */
    /* 0x3C */ MvC2Box hurtbox;
    /* 0x44 */ MvC2Box hitbox;
} MvC2Object;

#endif /* MVC2_OBJECT_H */
