#ifndef __BSP_POWER_H__
#define __BSP_POWER_H__

#include "stm32f10x.h"
#include <stdint.h>

void BSP_Power_Init(void);
void BSP_Power_HoldOn(void);
void BSP_Power_HoldOff(void);
void BSP_Power_ResumeHold(void);
uint8_t BSP_Power_KeyIsPressed(void);
uint8_t BSP_Power_BootCheckAndHold(void);
uint8_t BSP_Power_WaitKeyPressAndHold(uint32_t timeout_ms);
uint8_t BSP_Power_WaitKeyRelease(uint32_t timeout_ms);

#endif
