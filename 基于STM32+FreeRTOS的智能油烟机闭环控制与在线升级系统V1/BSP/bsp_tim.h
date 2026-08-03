#ifndef __BSP_TIM_H
#define __BSP_TIM_H

#include "bsp_sys.h"
#include <stdint.h>

/*
 * bsp_tim.h
 *
 * 当前项目只实际使用两个定时器：
 * - TIM2_CH1：PA0，电机 PWM 输出
 * - TIM4 编码器模式：PB6/PB7，电机测速
 *
 * 旧工程里遗留的 TIM3 舵机 PWM 已删除，因为它使用 PB0，
 * 会和当前蜂鸣器引脚冲突。
 */

void TIM2_PWM_CH1_Init(u16 arr, u16 psc);
void TIM2_PWM_CH1_SetCompare(u16 compare);
void TIM2_PWM_CH1_Stop(void);

void TIM4_EncoderMode_Init(void);
int16_t TIM4_Encoder_GetDeltaAndReset(void);

#endif /* __BSP_TIM_H */
