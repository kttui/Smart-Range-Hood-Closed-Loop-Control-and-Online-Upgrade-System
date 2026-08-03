#include "task_speed.h"
#include "speed_service.h"
#include "FreeRTOS.h"
#include "task.h"

void speed_task(void *pvParameters)
{
    TickType_t last_wake_time;

    (void)pvParameters;

    Speed_Service_Init();
    last_wake_time = xTaskGetTickCount();

    while (1)
    {
        /*
         * SpeedCalcTask 固定 50ms 读取一次编码器。
         * 使用 vTaskDelayUntil 可以减少周期漂移，让 RPM 计算里的 sample_ms 更稳定。
         */
        Speed_Service_Process();
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SPEED_SERVICE_PERIOD_MS));
    }
}
