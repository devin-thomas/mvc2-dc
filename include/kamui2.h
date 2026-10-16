#ifndef KAMUI2_H
#define KAMUI2_H

#include "types.h"

/* Sega Kamui2 3D Graphics Library for Dreamcast */

typedef struct {
    f32 x, y, z;
    u32 argb;
} KmVertex;

typedef struct {
    u32 list_type;
    u32 pass_num;
    void *surface_desc;
} KmRenderContext;

void kmInitDevice(u32 flags);
void kmSetDisplayMode(u32 mode, u32 bpp, u32 dither, u32 anti_alias);
void kmBeginScene(KmRenderContext *ctx);
void kmEndScene(KmRenderContext *ctx);

#endif /* KAMUI2_H */
