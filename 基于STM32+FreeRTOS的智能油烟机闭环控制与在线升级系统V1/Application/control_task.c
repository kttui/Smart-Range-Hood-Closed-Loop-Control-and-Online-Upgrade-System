#include "control_task.h"
#include "control_service.h"
#include "message_def.h"
#include "FreeRTOS.h"
#include "queue.h"

void control_task(void *pvParameters)
{
    SystemEvent_t event;

    (void)pvParameters;

    Control_Service_Init();

    while (1)
    {
        if (xQueueReceive(system_event_queue, &event, portMAX_DELAY) == pdTRUE)
        {
            Control_Service_ProcessEvent(&event);
        }
    }
}
