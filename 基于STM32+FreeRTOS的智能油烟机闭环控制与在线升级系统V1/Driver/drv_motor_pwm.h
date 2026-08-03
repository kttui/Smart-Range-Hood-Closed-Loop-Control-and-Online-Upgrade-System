#ifndef __DRV_MOTOR_PWM_H
#define __DRV_MOTOR_PWM_H

#include <stdint.h>

typedef enum
{
    MOTOR_DIR_FORWARD = 0,
    MOTOR_DIR_REVERSE
} MotorDirection_t;

int Motor_PWM_Init(void);
int Motor_PWM_Start(void);
int Motor_PWM_Stop(void);
int Motor_PWM_SetDirection(MotorDirection_t direction);
int Motor_PWM_SetDuty(uint16_t duty_permille);
uint16_t Motor_PWM_GetDuty(void);

#endif
