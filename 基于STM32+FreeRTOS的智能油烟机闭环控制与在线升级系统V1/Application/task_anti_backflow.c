#include "task_anti_backflow.h"
#include "anti_backflow_service.h"
#include "FreeRTOS.h"
#include "task.h"

void anti_backflow_task(void *pvParameters)
{
    TickType_t last_wake_time;

    (void)pvParameters;

    Anti_Backflow_Service_Init();
    last_wake_time = xTaskGetTickCount();

    while (1)
    {
        /*
         * AntiBackFlowTask 每 100ms 运行一次防回流状态机。
         * 状态机只给出 backflow_target_rpm，由 MotorControlTask 统一合成最终输出。
         */
        Anti_Backflow_Service_Process();
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(ANTI_BACKFLOW_SERVICE_PERIOD_MS));
    }
}
