#include "task_key.h"
#include "drv_key.h"
#include "event_service.h"
#include "FreeRTOS.h"
#include "task.h"

#define KEY_TASK_PERIOD_MS       10

static uint8_t Key_Task_MapEvent(const KeyEvent_t *key_event, SystemEventType_t *system_event)
{
    if (key_event == 0 || system_event == 0)
    {
        return 0;
    }

    /*
     * task_key 只做“按键动作 -> 系统事件”的映射。
     * 模式、档位、照明灯怎么变化，统一交给 control_service。
     */
    if (key_event->id == KEY_ID_MODE && key_event->action == KEY_ACTION_SHORT)
    {
        *system_event = SYS_EVT_KEY_MODE_SHORT;
        return 1;
    }

    if (key_event->id == KEY_ID_SPEED && key_event->action == KEY_ACTION_SHORT)
    {
        *system_event = SYS_EVT_KEY_SPEED_SHORT;
        return 1;
    }

    if (key_event->id == KEY_ID_LIGHT && key_event->action == KEY_ACTION_SHORT)
    {
        *system_event = SYS_EVT_KEY_LIGHT_SHORT;
        return 1;
    }

    if (key_event->id == KEY_ID_UPGRADE && key_event->action == KEY_ACTION_LONG)
    {
        *system_event = SYS_EVT_KEY_UPDATE_LONG;
        return 1;
    }

    return 0;
}

void key_task(void *pvParameters)
{
    KeyEvent_t key_event;
    SystemEventType_t system_event;

    (void)pvParameters;

    Key_Init();

    while (1)
    {
        if (Key_Scan(&key_event) &&
            Key_Task_MapEvent(&key_event, &system_event))
        {
            (void)Event_Publish(system_event, 0, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(KEY_TASK_PERIOD_MS));
    }
}
