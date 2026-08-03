#ifndef __CONTROL_SERVICE_H
#define __CONTROL_SERVICE_H

#include "message_def.h"

void Control_Service_Init(void);
void Control_Service_ProcessEvent(const SystemEvent_t *event);

#endif
