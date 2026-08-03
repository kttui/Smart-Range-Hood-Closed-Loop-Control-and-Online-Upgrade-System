#include "bsp_gpio.h"

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    uint16_t clk;
} HAL_gpio_map_t;

static const HAL_gpio_map_t hal_gpio_map[HAL_PIN_MAX] = {
    [HAL_PIN_LED_R] = {LED_R_PORT, LED_R_PIN, LED_R_GPIO_CLK},
    [HAL_PIN_LED_G] = {LED_G_PORT, LED_G_PIN, LED_G_GPIO_CLK},
    [HAL_PIN_LED_B] = {LED_B_PORT, LED_B_PIN, LED_B_GPIO_CLK},
    [HAL_PIN_LED_LIGHT] = {LED_LIGHT_PORT, LED_LIGHT_PIN, LED_LIGHT_GPIO_CLK},
    [HAL_PIN_BEEP_ALARM] = {BEEP_PORT, BEEP_PIN, BEEP_GPIO_CLK},
    [HAL_PIN_GAS_SENSOR] = {GAS_ADC_PORT, GAS_ADC_PIN, GAS_ADC_GPIO_CLK},
    [HAL_PIN_DHT11] = {DHT11_PORT, DHT11_PIN, DHT11_GPIO_CLK},
    [HAL_PIN_KEY_MODE] = {KEY_MODE_PORT, KEY_MODE_PIN, KEY_MODE_GPIO_CLK},
    [HAL_PIN_KEY_SPEED] = {KEY_SPEED_PORT, KEY_SPEED_PIN, KEY_SPEED_GPIO_CLK},
    [HAL_PIN_KEY_LIGHT] = {KEY_LIGHT_PORT, KEY_LIGHT_PIN, KEY_LIGHT_GPIO_CLK},
    [HAL_PIN_KEY_UPGRADE] = {KEY_UPGRADE_PORT, KEY_UPGRADE_PIN, KEY_UPGRADE_GPIO_CLK},
    [HAL_PIN_MOTOR_IN1] = {MOTOR_IN1_PORT, MOTOR_IN1_PIN, MOTOR_IN1_GPIO_CLK},
    [HAL_PIN_MOTOR_IN2] = {MOTOR_IN2_PORT, MOTOR_IN2_PIN, MOTOR_IN2_GPIO_CLK},
    [HAL_PIN_MOTOR_STBY] = {MOTOR_STBY_PORT, MOTOR_STBY_PIN, MOTOR_STBY_GPIO_CLK},
};
void HAL_GPIO_InitPin(HAL_Pin_t pin, HAL_GPIO_Mode_t mode)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(hal_gpio_map[pin].clk, ENABLE);
    gpio.GPIO_Pin = hal_gpio_map[pin].pin;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode =
        (mode == HAL_GPIO_MODE_OUTPUT_OD)      ? GPIO_Mode_Out_OD :
        (mode == HAL_GPIO_MODE_OUTPUT_PP)      ? GPIO_Mode_Out_PP :
        (mode == HAL_GPIO_MODE_ANALOG)         ? GPIO_Mode_AIN :
        (mode == HAL_GPIO_MODE_AF_PP)          ? GPIO_Mode_AF_PP :
        (mode == HAL_GPIO_MODE_AF_OD)          ? GPIO_Mode_AF_OD :
        (mode == HAL_GPIO_MODE_INPUT_PU)       ? GPIO_Mode_IPU :
        (mode == HAL_GPIO_MODE_INPUT_PD)       ? GPIO_Mode_IPD :GPIO_Mode_IN_FLOATING;

    GPIO_Init(hal_gpio_map[pin].port, &gpio);
}
void HAL_GPIO_WritePin(HAL_Pin_t pin, uint8_t level)
{
    if (level)
        GPIO_SetBits(hal_gpio_map[pin].port, hal_gpio_map[pin].pin);
    else
        GPIO_ResetBits(hal_gpio_map[pin].port, hal_gpio_map[pin].pin);
}
uint8_t HAL_GPIO_ReadPin(HAL_Pin_t pin)
{
    return GPIO_ReadInputDataBit(hal_gpio_map[pin].port, hal_gpio_map[pin].pin);
}
