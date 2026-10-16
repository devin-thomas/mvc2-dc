#include "types.h"
#include "sh4.h"
#include "shinobi.h"
#include "kamui2.h"
#include "cri_adx.h"
#include "mvc2/object.h"
#include "mvc2/characters.h"

/*
 * Marvel vs. Capcom 2 (Dreamcast NA) - Engine Entry and Main Loop
 */

void game_init(void) {
    /* Initialize Shinobi SDK and hardware peripherals */
    syHwInit();

    /* Initialize CRI ADX Sound Stream */
    ADXF_Init();

    /* Initialize Kamui2 PowerVR2 Graphics Pipeline */
    kmInitDevice(0);
}

void game_loop(void) {
    /* Main fighting loop tick */
}
