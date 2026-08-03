#ifndef __EVENT_SERVICE_H
#define __EVENT_SERVICE_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "message_def.h"

/**
 * @brief 任务上下文统一投递系统事件。
 *
 * wait_ticks 为队列满时的最长等待 tick 数；传 0 表示不等待。
 * 后续统计事件丢失、记录事件轨迹时，只需要在 event_service 内集中扩展。
 */
BaseType_t Event_Publish(SystemEventType_t type, uint8_t data, TickType_t wait_ticks);

/**
 * @brief 中断上下文统一投递系统事件。
 *
 * woken 直接传给 FreeRTOS FromISR API，用于按需触发任务切换。
 */
BaseType_t Event_PublishFromISR(SystemEventType_t type,
                                uint8_t data,
                                BaseType_t *woken);

#endif



