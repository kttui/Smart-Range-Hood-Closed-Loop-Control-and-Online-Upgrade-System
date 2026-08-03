#include "control_service.h"
#include "drv_actuator.h"
#include "log_service.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "task_ota.h"

/*
 * control_service.c
 *
 * 控制服务只处理“离散控制事件”：
 * - MODE 短按切换系统模式
 * - SPEED 短按切换手动档位
 * - LIGHT 短按切换照明灯
 * - UPGRADE 长按进入升级等待状态
 *
 * 自动调速、防回流、电机 PWM 不放在这里，分别由 auto_control_service、
 * anti_backflow_service、motor_control_service 周期处理。
 */

#define CONTROL_KEY_BEEP_MS       60

static void Control_Service_LockState(void)
{
    if (sys_data_mutex != 0)
    {
        (void)xSemaphoreTake(sys_data_mutex, portMAX_DELAY);
    }
}

static void Control_Service_UnlockState(void)
{
    if (sys_data_mutex != 0)
    {
        xSemaphoreGive(sys_data_mutex);
    }
}

static RGBColor_t Control_Service_ModeToRgb(SystemMode_t mode)
{
    /*
     * RGB 状态灯约定：
     * OFF    -> 灭灯
     * MANUAL -> 蓝色
     * AUTO   -> 绿色
     * UPDATE -> 紫色
     * 其他异常状态 -> 红色
     */
    switch (mode)
    {
    case SYS_MODE_MANUAL:
        return RGB_COLOR_BLUE;

    case SYS_MODE_AUTO:
        return RGB_COLOR_GREEN;

    case SYS_MODE_UPDATE:
        return RGB_COLOR_RED;

    case SYS_MODE_OFF:
        return RGB_COLOR_OFF;

    default:
        return RGB_COLOR_RED;
    }
}

static const char *Control_Service_ModeText(SystemMode_t mode)
{
    switch (mode)
    {
    case SYS_MODE_OFF:
        return "OFF";
    case SYS_MODE_MANUAL:
        return "MANUAL";
    case SYS_MODE_AUTO:
        return "AUTO";
    case SYS_MODE_UPDATE:
        return "UPDATE";
    default:
        return "OTHER";
    }
}

static const char *Control_Service_FanText(FanLevel_t level)
{
    switch (level)
    {
    case FAN_LEVEL_STOP:
        return "STOP";
    case FAN_LEVEL_LOW:
        return "LOW";
    case FAN_LEVEL_MID:
        return "MID";
    case FAN_LEVEL_HIGH:
        return "HIGH";
    default:
        return "UNKNOWN";
    }
}

static void Control_Service_SetLight(uint8_t on)
{
    ActuatorValue_t value;

    value.on = on ? 1 : 0;
    value.rgb_color = RGB_COLOR_OFF;
    (void)Actuator_Set(&g_led_light_actuator, &value);
}

static void Control_Service_BeepOnce(void)
{
    ActuatorValue_t value;

    /*
     * 按键提示音属于人机交互反馈。
     * 蜂鸣时间很短，直接在 control_task 中延时关闭即可，
     * 不额外创建蜂鸣器任务，避免小系统任务数量继续膨胀。
     */
    value.rgb_color = RGB_COLOR_OFF;

    value.on = 1;
    (void)Actuator_Set(&g_beep_actuator, &value);

    vTaskDelay(pdMS_TO_TICKS(CONTROL_KEY_BEEP_MS));

    value.on = 0;
    (void)Actuator_Set(&g_beep_actuator, &value);
}

static void Control_Service_HandleModeShort(void)
{
    SystemMode_t next_mode;

    Control_Service_LockState();

    switch (g_system_state.mode)
    {
    case SYS_MODE_OFF:
        next_mode = SYS_MODE_MANUAL;
        break;

    case SYS_MODE_MANUAL:
        next_mode = SYS_MODE_AUTO;
        break;

    case SYS_MODE_AUTO:
    default:
        next_mode = SYS_MODE_OFF;
        break;
    }

    g_system_state.mode = next_mode;
    g_system_state.output.rgb_state = (uint8_t)Control_Service_ModeToRgb(next_mode);

    if (next_mode == SYS_MODE_OFF)
    {
        /*
         * OFF 模式必须清空手动档位。
         * motor_control_service 下一周期会看到 target=0 并停止电机。
         */
        g_system_state.fan_level = FAN_LEVEL_STOP;
    }

    Control_Service_UnlockState();

    (void)RGB_Led_SetColor(Control_Service_ModeToRgb(next_mode));
    LOG_INFO("KEY MODE -> %s", Control_Service_ModeText(next_mode));
}

static void Control_Service_HandleSpeedShort(void)
{
    FanLevel_t next_level;
    SystemMode_t mode;

    Control_Service_LockState();

    mode = g_system_state.mode;
    if (mode != SYS_MODE_MANUAL)
    {
        Control_Service_UnlockState();
        LOG_WARN("KEY SPEED ignored in %s mode", Control_Service_ModeText(mode));
        return;
    }

    switch (g_system_state.fan_level)
    {
    case FAN_LEVEL_STOP:
        next_level = FAN_LEVEL_LOW;
        break;

    case FAN_LEVEL_LOW:
        next_level = FAN_LEVEL_MID;
        break;

    case FAN_LEVEL_MID:
        next_level = FAN_LEVEL_HIGH;
        break;

    case FAN_LEVEL_HIGH:
    default:
        next_level = FAN_LEVEL_STOP;
        break;
    }

    g_system_state.fan_level = next_level;

    Control_Service_UnlockState();

    LOG_INFO("KEY SPEED -> %s", Control_Service_FanText(next_level));
}

static void Control_Service_HandleLightShort(void)
{
    uint8_t next_light;

    Control_Service_LockState();

    next_light = g_system_state.output.light_on ? 0 : 1;
    g_system_state.output.light_on = next_light;

    Control_Service_UnlockState();

    Control_Service_SetLight(next_light);
    LOG_INFO("KEY LIGHT -> %s", next_light ? "ON" : "OFF");
}

static void Control_Service_HandleUpgradeLong(void)
{
    Ota_Task_RequestCheck();
    LOG_INFO("KEY UPGRADE long press -> check remote firmware");
}

void Control_Service_Init(void)
{
    ActuatorValue_t light_value;

    /*
     * 输出执行器统一在控制服务初始化：
     * RGB 状态灯、照明灯、蜂鸣器都先进入明确的关闭状态，
     * 再根据 g_system_state 刷新当前显示。
     */
    (void)Actuator_Init(&g_rgb_led_actuator);
    (void)Actuator_Init(&g_led_light_actuator);
    (void)Actuator_Init(&g_beep_actuator);

    light_value.on = g_system_state.output.light_on ? 1 : 0;
    light_value.rgb_color = RGB_COLOR_OFF;
    (void)Actuator_Set(&g_led_light_actuator, &light_value);

    (void)RGB_Led_SetColor(Control_Service_ModeToRgb(g_system_state.mode));
}

void Control_Service_ProcessEvent(const SystemEvent_t *event)
{
    if (event == 0)
    {
        return;
    }

    switch (event->event_type)
    {
    case SYS_EVT_KEY_MODE_SHORT:
        Control_Service_BeepOnce();
        Control_Service_HandleModeShort();
        break;

    case SYS_EVT_KEY_SPEED_SHORT:
        Control_Service_BeepOnce();
        Control_Service_HandleSpeedShort();
        break;

    case SYS_EVT_KEY_LIGHT_SHORT:
        Control_Service_BeepOnce();
        Control_Service_HandleLightShort();
        break;

    case SYS_EVT_KEY_UPDATE_LONG:
        Control_Service_HandleUpgradeLong();
        break;

    default:
        break;
    }
}
