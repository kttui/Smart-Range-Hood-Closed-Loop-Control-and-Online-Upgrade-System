#ifndef __TASK_STACK_DEBUG_H
#define __TASK_STACK_DEBUG_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

/*
 * 任务栈水位调试开关。
 *
 * 当前工程已经重新接入 log_service.c 和 task_log.c，
 * app_main.c 会创建 log_queue 和 log_task。
 * 打开该开关后，使用 LOG_INFO() 周期打印任务剩余栈空间，
 * 方便观察任务栈是否分配过小。
 *
 * 如果后续为了减少串口输出或节省 RAM，可以把该宏改为 0。
 */
#define TASK_STACK_DEBUG_ENABLE          1
#define TASK_STACK_DEBUG_PERIOD_MS       10000

#if (TASK_STACK_DEBUG_ENABLE != 0)
#include "log_service.h"
#endif

static void Task_StackDebugLog(const char *task_name, TickType_t *last_tick)
{
#if (TASK_STACK_DEBUG_ENABLE != 0) && (INCLUDE_uxTaskGetStackHighWaterMark == 1)
    TickType_t now;

    if (task_name == 0 || last_tick == 0)
    {
        return;
    }

    now = xTaskGetTickCount();
    if ((*last_tick == 0) ||
        ((now - *last_tick) >= pdMS_TO_TICKS(TASK_STACK_DEBUG_PERIOD_MS)))
    {
        *last_tick = now;
        LOG_INFO("STACK %s free=%u words",
                 task_name,
                 (unsigned int)uxTaskGetStackHighWaterMark(NULL));
    }
#else
    (void)task_name;
    (void)last_tick;
#endif
}

#endif /* __TASK_STACK_DEBUG_H */
