#ifndef __SPEED_SERVICE_H
#define __SPEED_SERVICE_H

#include "stm32f10x.h"

#define SPEED_SERVICE_PERIOD_MS      50

void Speed_Service_Init(void);
void Speed_Service_Process(void);

#endif /* __SPEED_SERVICE_H */
