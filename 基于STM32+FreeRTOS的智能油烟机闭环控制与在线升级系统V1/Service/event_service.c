#include "event_service.h"
#include "queue.h"

/**
 * @file event_service.c
 * @brief 系统事件投递统一入口。
 *
 * 业务模块只关心事件类型和数据，不直接操作 system_event_queue。
 * 后续队列满处理、事件丢失统计或事件追踪都可以集中放在这里。
 */

static void Event_Build(SystemEvent_t *event,
                        SystemEventType_t type,
                        uint8_t data)
{
    event->event_type = type;
    event->event_data = data;
}

BaseType_t Event_Publish(SystemEventType_t type, uint8_t data, TickType_t wait_ticks)
{
    SystemEvent_t event;

    if (system_event_queue == NULL)
    {
        return pdFAIL;
    }

    Event_Build(&event, type, data);

    return xQueueSend(system_event_queue, &event, wait_ticks);
}

BaseType_t Event_PublishFromISR(SystemEventType_t type,
                                uint8_t data,
                                BaseType_t *woken)
{
    SystemEvent_t event;

    if (system_event_queue == NULL)
    {
        return pdFAIL;
    }

    Event_Build(&event, type, data);

    return xQueueSendFromISR(system_event_queue, &event, woken);
}
