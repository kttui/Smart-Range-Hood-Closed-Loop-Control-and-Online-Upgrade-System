#include "power_task.h"
#include "bsp_power.h"
#include "FreeRTOS.h"
#include "task.h"

/* 电源键扫描周期和长按关机判定时间。 */
#define POWER_KEY_SCAN_PERIOD_MS       10
#define POWER_KEY_LONG_PRESS_MS        2000

void power_task(void *pvParameters)
{
    uint16_t press_time_ms = 0;
    uint8_t wait_first_release = 1;

    (void)pvParameters;

    while (1)
    {
        /*
         * 系统刚启动时可能还按着电源键，或者自锁电路让按键输入短时间保持低电平。
         * 先等到第一次松手，再开始统计长按关机，避免刚开机就被误判为长按关机。
         */
        if (wait_first_release)
        {
            if (!BSP_Power_KeyIsPressed())
            {
								wait_first_release = 0;
            }

            vTaskDelay(pdMS_TO_TICKS(POWER_KEY_SCAN_PERIOD_MS));
            continue;
        }

        if (BSP_Power_KeyIsPressed())
        {
            if (press_time_ms < POWER_KEY_LONG_PRESS_MS)
            {
                press_time_ms += POWER_KEY_SCAN_PERIOD_MS;
            }

            if (press_time_ms >= POWER_KEY_LONG_PRESS_MS)
            {
//							  wait_first_release = 1;//允许长按开机
								BSP_Power_HoldOff();
							
                taskDISABLE_INTERRUPTS();
                while (1)
                {
                    /*
                     * 注意：
                     * 手还按着电源键时，硬件按键仍然会暂时供电。
                     * 松开按键后，12V_SYS 才会真正断电。
                     */
                }
            }
        }
        else
        {
            press_time_ms = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(POWER_KEY_SCAN_PERIOD_MS));
    }
}
