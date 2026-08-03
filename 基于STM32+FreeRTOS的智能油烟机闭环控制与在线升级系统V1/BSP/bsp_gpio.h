#ifndef __BSP_GPIO_H
#define __BSP_GPIO_H

#include "stm32f10x.h"
#include "stdint.h"
#include "board_pins.h"


typedef enum{
    HAL_PIN_LED_R = 0,
    HAL_PIN_LED_G,
    HAL_PIN_LED_B,
    HAL_PIN_LED_LIGHT,
    HAL_PIN_BEEP_ALARM,
    HAL_PIN_GAS_SENSOR,
    HAL_PIN_DHT11,
    HAL_PIN_KEY_MODE,
    HAL_PIN_KEY_SPEED,
    HAL_PIN_KEY_LIGHT,
    HAL_PIN_KEY_UPGRADE,
    HAL_PIN_MOTOR_IN1,
    HAL_PIN_MOTOR_IN2,
    HAL_PIN_MOTOR_STBY,
    HAL_PIN_MAX
} HAL_Pin_t;
typedef enum{
    HAL_GPIO_MODE_INPUT = 0,
    HAL_GPIO_MODE_OUTPUT_PP,
    HAL_GPIO_MODE_OUTPUT_OD,
    HAL_GPIO_MODE_ANALOG,
    HAL_GPIO_MODE_AF_PP,
    HAL_GPIO_MODE_INPUT_FLOATING,
    HAL_GPIO_MODE_INPUT_PU,
    HAL_GPIO_MODE_INPUT_PD,
    HAL_GPIO_MODE_AF_OD
}HAL_GPIO_Mode_t;


void HAL_GPIO_InitPin(HAL_Pin_t pin, HAL_GPIO_Mode_t mode);
void HAL_GPIO_WritePin(HAL_Pin_t pin, uint8_t level);
uint8_t HAL_GPIO_ReadPin(HAL_Pin_t pin);
#endif
