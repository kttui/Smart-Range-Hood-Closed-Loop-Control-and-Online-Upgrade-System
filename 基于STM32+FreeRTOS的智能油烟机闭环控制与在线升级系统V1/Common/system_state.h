#ifndef __SYSTEM_STATE_H
#define __SYSTEM_STATE_H

#include "stm32f10x.h"
#include "drv_sensor.h"

/* ===================== 系统工作模式 ===================== */
typedef enum
{
    SYS_MODE_OFF = 0,
    SYS_MODE_MANUAL,
    SYS_MODE_AUTO,
    SYS_MODE_ALARM,
    SYS_MODE_UPDATE,
    SYS_MODE_ERROR
} SystemMode_t;

/* ===================== 手动风机档位 ===================== */
typedef enum
{
    FAN_LEVEL_STOP = 0,
    FAN_LEVEL_LOW,
    FAN_LEVEL_MID,
    FAN_LEVEL_HIGH
} FanLevel_t;

/*
 * SensorData_t 由 drv_sensor.h 统一定义。
 * 系统状态直接复用驱动层采样结构，避免 Common/Driver 重复 typedef 同名结构。
 * ARMCC 会把两个同名但来源不同的 typedef 当作不同类型，容易导致指针参数不兼容。
 */

/* ===================== 电机运行数据 ===================== */
typedef struct
{
    int16_t current_rpm;          /* 编码器测速任务写入的当前转速，单位 RPM。 */
    int16_t encoder_delta;        /* 最近一个测速周期的编码器原始计数差，用于判断是否有脉冲输入。 */
    int16_t target_rpm;           /* 电机控制任务最终采用的目标转速，单位 RPM。 */

    int16_t auto_target_rpm;      /* 自动融合算法给出的建议转速，仅在 AUTO 模式下有效。 */
    int16_t backflow_target_rpm;  /* 防回流状态机给出的兜底转速，高于普通自动目标时优先。 */

    uint16_t pwm_duty;            /* 当前 PWM 占空比，范围 0~1000，对应 0.0%~100.0%。 */
    uint16_t auto_factor;         /* 自动融合综合系数 F，范围 0~1000，便于串口/LCD观察。 */

    uint8_t enabled;              /* 电机是否处于使能运行状态。 */
    uint8_t direction;            /* 电机方向，取值来自 MotorDirection_t。 */

    uint8_t auto_state;           /* 自动调速状态机当前状态，便于 UI 和调试打印。 */
    uint8_t backflow_state;       /* 防回流状态机当前状态，便于 UI 和调试打印。 */
    uint8_t backflow_active;      /* 防回流是否正在接管/抬高目标转速。 */

    uint8_t encoder_a_level;      /* ENCODER_A/PB7 当前电平，用于现场排查编码器接线。 */
    uint8_t encoder_b_level;      /* ENCODER_B/PB6 当前电平，用于现场排查编码器接线。 */
    uint16_t speed_sample_count;  /* 测速任务运行计数，持续增加说明 SpeedCalcTask 正在执行。 */
} MotorData_t;

/* ===================== UI / 输出状态 ===================== */
typedef struct
{
    uint8_t light_on;             /* 烟机照明灯状态。 */
    uint8_t buzzer_on;            /* 蜂鸣器状态。 */
    uint8_t rgb_state;            /* RGB 状态灯状态。 */
} OutputState_t;

/* ===================== 系统全局状态 ===================== */
typedef struct
{
    SystemMode_t mode;
    FanLevel_t fan_level;

    SensorData_t sensor;
    MotorData_t motor;
    OutputState_t output;

    uint8_t alarm_flag;
    uint8_t power_off_request;
} SystemState_t;

/* 系统全局状态实例在 app_main.c 中定义，其他模块通过 extern 引用。 */
extern SystemState_t g_system_state;

#endif /* __SYSTEM_STATE_H */
