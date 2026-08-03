#ifndef __BSP_ADC_H__
#define __BSP_ADC_H__

#include "stm32f10x.h"
#include <stdint.h>

typedef enum
{
    HAL_ADC_IN_GAS_SENSOR = 0,
    HAL_ADC_IN_MAX
} HAL_AdcInput_t;

void HAL_ADC_GlobalInit(void);
void HAL_ADC_InputInit(HAL_AdcInput_t input);
uint16_t HAL_ADC_Read(HAL_AdcInput_t input);
uint16_t HAL_ADC_ReadAverage(HAL_AdcInput_t input, uint8_t times);

#endif
