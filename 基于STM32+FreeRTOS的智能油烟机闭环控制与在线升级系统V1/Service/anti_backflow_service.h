#ifndef __ANTI_BACKFLOW_SERVICE_H
#define __ANTI_BACKFLOW_SERVICE_H

#include "stm32f10x.h"

#define ANTI_BACKFLOW_SERVICE_PERIOD_MS      100

typedef enum
{
    BACKFLOW_IDLE = 0,
    BACKFLOW_MONITORING,
    BACKFLOW_ACTIVE,
    BACKFLOW_RECOVERY
} BackflowState_t;

void Anti_Backflow_Service_Init(void);
void Anti_Backflow_Service_Process(void);

#endif /* __ANTI_BACKFLOW_SERVICE_H */
