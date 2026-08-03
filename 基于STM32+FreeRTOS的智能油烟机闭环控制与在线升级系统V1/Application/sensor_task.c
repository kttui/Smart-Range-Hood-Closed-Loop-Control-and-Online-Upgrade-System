#include "sensor_task.h"
#include "sensor_service.h"
#include "task_stack_debug.h"
#include "FreeRTOS.h"
#include "task.h"

/**
 * @file sensor_task.c
 * @brief 传感器应用任务。
 *
 * Application 层只负责任务调度：决定什么时候采集。
 * 具体采集、过滤、更新系统数据和串口打印由 sensor_service 完成。
 */

/* 传感器采集周期。 */
#define SENSOR_SAMPLE_PERIOD_MS       2000

/**
 * @brief 传感器任务入口。
 */
void sensor_task(void *pvParameters)
{
    TickType_t last_stack_log_tick = 0;

    (void)pvParameters;

    Sensor_Service_Init();//初始化传感器服务，完成底层传感器设备的初始化

    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1)
    {
        Sensor_Service_Process();//执行一次传感器读取、系统数据更新和串口打印。
        Task_StackDebugLog("sensor_task", &last_stack_log_tick);
        vTaskDelay(pdMS_TO_TICKS(SENSOR_SAMPLE_PERIOD_MS));
    }
}
