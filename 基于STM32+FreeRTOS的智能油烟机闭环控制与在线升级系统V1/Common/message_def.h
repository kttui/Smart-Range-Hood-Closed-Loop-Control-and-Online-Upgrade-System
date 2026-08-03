#ifndef __MESSAGE_DEF_H
#define __MESSAGE_DEF_H

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "stm32f10x.h"
#include "system_state.h"

/* ===================== 系统事件类型 =====================
 * 事件只描述“发生了什么”，不直接包含业务处理逻辑。
 * key_task 负责产生按键事件，control_task 负责消费事件并修改系统状态。
 */
typedef enum
{
    SYS_NONE = 0,

    /* 按键事件 */
    SYS_EVT_KEY_MODE_SHORT,       /* MODE 短按：OFF -> MANUAL -> AUTO -> OFF。 */
    SYS_EVT_KEY_MODE_LONG,        /* MODE 长按：当前未使用，预留。 */
    SYS_EVT_KEY_SPEED_SHORT,      /* SPEED 短按：手动模式下 STOP/LOW/MID/HIGH 循环。 */
    SYS_EVT_KEY_LIGHT_SHORT,      /* LIGHT 短按：照明灯开关切换。 */
    SYS_EVT_KEY_UPDATE_LONG,      /* UPGRADE 长按：进入升级等待状态。 */

    /* 传感器事件，当前传感器任务主要直接更新 g_system_state。 */
    SYS_EVT_SENSOR_UPDATED,
    SYS_EVT_GAS_ALARM,
    SYS_EVT_GAS_NOMAL,

    /* 电机事件，当前开环/PID 控制主要直接写 g_system_state。 */
    SYS_EVT_MOTOR_SPEED_UPDATED,
    SYS_EVT_MOTOR_FAULT,

    /* 控制事件 */
    SYS_EVT_TARGET_RPM_UPDATED,
    SYS_EVT_MODE_CHANGED,

    /* 电源事件 */
    SYS_EVT_POWER_OFF_REQUEST,

    /* 在线升级事件。 */
    SYS_EVT_UPGRADE_REQUEST,
    SYS_EVT_UPGRADE_AVAILABLE,
    SYS_EVT_UPGRADE_ACCEPTED,
    SYS_EVT_UPGRADE_DONE,
    SYS_EVT_UPGRADE_FAILED
} SystemEventType_t;

typedef struct
{
    SystemEventType_t event_type;
    uint8_t event_data;
} SystemEvent_t;

/* 全局共享资源在 app_main.c 中创建，这里统一 extern，避免每个模块重复声明。 */
extern SystemState_t g_system_state;
extern SemaphoreHandle_t sys_data_mutex;
extern QueueHandle_t system_event_queue;
extern SemaphoreHandle_t uart_mutex;

#endif /* __MESSAGE_DEF_H */
