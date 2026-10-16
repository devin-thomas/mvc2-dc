#ifndef TYPES_H
#define TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Standard integer types for Sega Dreamcast (Hitachi SH-4 32-bit RISC) */
typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;

typedef int8_t    s8;
typedef int16_t   s16;
typedef int32_t   s32;
typedef int64_t   s64;

typedef float     f32;
typedef double    f64;

/* Fixed-point 16.16 representation used by Capcom fighting engine */
typedef s32       fixed32;
#define TO_FIXED(x)   ((fixed32)((x) * 65536.0f))
#define FROM_FIXED(x) ((float)(x) / 65536.0f)

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#endif /* TYPES_H */
