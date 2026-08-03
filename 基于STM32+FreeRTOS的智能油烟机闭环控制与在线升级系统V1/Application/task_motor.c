#include "task_motor.h"
#include "motor_control_service.h"
#include "FreeRTOS.h"
#include "task.h"

void motor_task(void *pvParameters)
{
    TickType_t last_wake_time;

    (void)pvParameters;

    Motor_Control_Service_Init();
    last_wake_time = xTaskGetTickCount();

    while (1)
    {
        /*
         * MotorControlTask 固定 50ms 输出一次 PWM。
         * 这里不再采集传感器、不再计算防回流，只负责把各服务给出的目标转速
         * 合成为最终 target_rpm，并执行开环 PWM 或 PID 闭环控制。
         */
        Motor_Control_Service_Process();
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(MOTOR_CONTROL_SERVICE_PERIOD_MS));
    }
}
