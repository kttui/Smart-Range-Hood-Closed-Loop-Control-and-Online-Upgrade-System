#include "task_ui.h"
#include "ui_service.h"
#include "FreeRTOS.h"
#include "task.h"

#define UI_TASK_REFRESH_MS       500

void ui_task(void *pvParameters)
{
    (void)pvParameters;

    /*
     * UI 初始化放在 UI 任务里做，避免启动阶段阻塞其它外设初始化。
     * LCD 初始化会复位屏幕并清屏，软件 SPI 速度有限，因此不放在中断或控制服务里。
     */
    UI_Service_Init();

    while (1)
    {
        /*
         * 500ms 刷新一次足够观察烟机状态。
         * 刷太快会占用软件 SPI 时间，也可能影响串口和电机任务的实时性。
         */
        UI_Service_Process();
        vTaskDelay(pdMS_TO_TICKS(UI_TASK_REFRESH_MS));
    }
}
