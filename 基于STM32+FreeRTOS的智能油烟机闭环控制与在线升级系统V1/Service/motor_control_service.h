#ifndef __MOTOR_CONTROL_SERVICE_H
#define __MOTOR_CONTROL_SERVICE_H

#include "stm32f10x.h"

#define MOTOR_CONTROL_SERVICE_PERIOD_MS      50

void Motor_Control_Service_Init(void);
void Motor_Control_Service_Process(void);

#endif /* __MOTOR_CONTROL_SERVICE_H */
