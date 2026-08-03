#include "task_auto.h"
#include "auto_control_service.h"
#include "FreeRTOS.h"
#include "task.h"

void auto_control_task(void *pvParameters)
{
    TickType_t last_wake_time;

    (void)pvParameters;

    Auto_Control_Service_Init();
    last_wake_time = xTaskGetTickCount();

    while (1)
    {
        /*
         * AutoControlTask 每 100ms 计算一次多传感器融合目标。
         * 它只写 auto_target_rpm，不直接操作电机 PWM，避免控制职责混乱。
         */
        Auto_Control_Service_Process();
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(AUTO_CONTROL_SERVICE_PERIOD_MS));
    }
}
