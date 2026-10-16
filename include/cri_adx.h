#ifndef CRI_ADX_H
#define CRI_ADX_H

#include "types.h"

/* CRI ADX Audio Middleware API */

typedef struct {
    u32 sample_rate;
    u16 channels;
    u16 block_size;
} AdxHeader;

void ADXF_Init(void);
void ADXF_Stop(void);
void ADXT_StartStream(const char *path);
void ADXT_StopStream(void);

#endif /* CRI_ADX_H */
