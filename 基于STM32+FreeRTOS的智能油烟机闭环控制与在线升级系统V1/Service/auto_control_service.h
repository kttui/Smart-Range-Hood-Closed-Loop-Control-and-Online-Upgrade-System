#ifndef __AUTO_CONTROL_SERVICE_H
#define __AUTO_CONTROL_SERVICE_H

#include "stm32f10x.h"

#define AUTO_CONTROL_SERVICE_PERIOD_MS      100

typedef enum
{
    AUTO_CONTROL_STATE_IDLE = 0,
    AUTO_CONTROL_STATE_MIN_RUN,
    AUTO_CONTROL_STATE_ADJUST
} AutoControlState_t;

void Auto_Control_Service_Init(void);
void Auto_Control_Service_Process(void);

#endif /* __AUTO_CONTROL_SERVICE_H */
