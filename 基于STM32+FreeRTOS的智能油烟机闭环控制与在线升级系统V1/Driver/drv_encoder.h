#ifndef __DRV_ENCODER_H
#define __DRV_ENCODER_H

#include <stdint.h>

void Encoder_Init(void);
int16_t Encoder_GetDeltaAndReset(void);
int16_t Encoder_CalcRpm(int16_t delta_count, uint16_t sample_ms);

#endif
