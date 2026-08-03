#include "drv_motor_pwm.h"
#include "bsp_gpio.h"
#include "bsp_tim.h"

#define MOTOR_PWM_PERIOD_TICKS      3599u
#define MOTOR_PWM_PRESCALER         0u
#define MOTOR_PWM_DUTY_MAX          1000u

static uint8_t s_motor_pwm_inited = 0;
static uint16_t s_motor_pwm_duty = 0;
static MotorDirection_t s_motor_direction = MOTOR_DIR_FORWARD;

static void Motor_PWM_WriteDirectionPins(MotorDirection_t direction)
{
    if (direction == MOTOR_DIR_FORWARD)
    {
        HAL_GPIO_WritePin(HAL_PIN_MOTOR_IN1, 1);
        HAL_GPIO_WritePin(HAL_PIN_MOTOR_IN2, 0);
    }
    else
    {
        HAL_GPIO_WritePin(HAL_PIN_MOTOR_IN1, 0);
        HAL_GPIO_WritePin(HAL_PIN_MOTOR_IN2, 1);
    }
}

int Motor_PWM_Init(void)
{
    /*
     * Driver 层只关心 TB6612 的 GPIO 和 PWM 输出。
     * 目标转速、PID 和工作模式判断放到 motor_control_service，避免驱动层混入业务规则。
     */
    HAL_GPIO_InitPin(HAL_PIN_MOTOR_IN1, HAL_GPIO_MODE_OUTPUT_PP);
    HAL_GPIO_InitPin(HAL_PIN_MOTOR_IN2, HAL_GPIO_MODE_OUTPUT_PP);
    HAL_GPIO_InitPin(HAL_PIN_MOTOR_STBY, HAL_GPIO_MODE_OUTPUT_PP);

    HAL_GPIO_WritePin(HAL_PIN_MOTOR_IN1, 0);
    HAL_GPIO_WritePin(HAL_PIN_MOTOR_IN2, 0);
    HAL_GPIO_WritePin(HAL_PIN_MOTOR_STBY, 0);

    TIM2_PWM_CH1_Init(MOTOR_PWM_PERIOD_TICKS, MOTOR_PWM_PRESCALER);
    TIM2_PWM_CH1_SetCompare(0);

    s_motor_pwm_duty = 0;
    s_motor_direction = MOTOR_DIR_FORWARD;
    s_motor_pwm_inited = 1;

    return 0;
}

int Motor_PWM_Start(void)
{
    if (!s_motor_pwm_inited)
    {
        (void)Motor_PWM_Init();
    }

    Motor_PWM_WriteDirectionPins(s_motor_direction);
    HAL_GPIO_WritePin(HAL_PIN_MOTOR_STBY, 1);

    return 0;
}

int Motor_PWM_Stop(void)
{
    if (!s_motor_pwm_inited)
    {
        return 0;
    }

    s_motor_pwm_duty = 0;
    TIM2_PWM_CH1_Stop();

    /*
     * TB6612 的 STBY 拉低后进入待机，同时 IN1/IN2 拉低，避免停机时误驱动。
     */
    HAL_GPIO_WritePin(HAL_PIN_MOTOR_IN1, 0);
    HAL_GPIO_WritePin(HAL_PIN_MOTOR_IN2, 0);
    HAL_GPIO_WritePin(HAL_PIN_MOTOR_STBY, 0);

    return 0;
}

int Motor_PWM_SetDirection(MotorDirection_t direction)
{
    if (direction != MOTOR_DIR_FORWARD && direction != MOTOR_DIR_REVERSE)
    {
        return -1;
    }

    s_motor_direction = direction;

    if (s_motor_pwm_inited)
    {
        Motor_PWM_WriteDirectionPins(direction);
    }

    return 0;
}

int Motor_PWM_SetDuty(uint16_t duty_permille)
{
    uint32_t compare;

    if (!s_motor_pwm_inited)
    {
        (void)Motor_PWM_Init();
    }

    if (duty_permille > MOTOR_PWM_DUTY_MAX)
    {
        duty_permille = MOTOR_PWM_DUTY_MAX;
    }

    s_motor_pwm_duty = duty_permille;
    compare = ((uint32_t)(MOTOR_PWM_PERIOD_TICKS + 1u) * duty_permille) / MOTOR_PWM_DUTY_MAX;

    if (compare > MOTOR_PWM_PERIOD_TICKS)
    {
        compare = MOTOR_PWM_PERIOD_TICKS;
    }

    TIM2_PWM_CH1_SetCompare((uint16_t)compare);

    return 0;
}

uint16_t Motor_PWM_GetDuty(void)
{
    return s_motor_pwm_duty;
}
