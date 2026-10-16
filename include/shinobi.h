#ifndef SHINOBI_H
#define SHINOBI_H

#include "types.h"

/* Sega Shinobi Library for Dreamcast v1.62 */

typedef struct {
    u32 version;
    u32 heap_size;
    void *heap_addr;
} ShinobiConfig;

/* Memory management */
void *syMalloc(u32 size);
void  syFree(void *ptr);

/* System hardware initialization */
void  syHwInit(void);
void  syStartInit(void);

/* Cache management */
void  syCachePrefetch(void *addr);
void  syCacheFlush(void *addr, u32 size);

/* Real-Time Clock */
u32   syRtcGet(void);

#endif /* SHINOBI_H */
