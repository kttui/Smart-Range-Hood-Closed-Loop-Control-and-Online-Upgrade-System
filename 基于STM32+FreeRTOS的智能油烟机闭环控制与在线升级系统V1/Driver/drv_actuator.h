#ifndef __DRV_ACTUATOR_H
#define __DRV_ACTUATOR_H

/**
 * @file drv_actuator.h
 * @brief 执行器驱动抽象接口。
 *
 * Driver 层只封装具体硬件设备的初始化、设置和读取。
 * 控制规则应放在 control_service，不应放在驱动层。
 */

#include <stdint.h>

/**
 * @brief 执行器驱动返回值。
 */
typedef enum
{
    ACTUATOR_OK = 0,
    ACTUATOR_ERR_PARAM = -1,
    ACTUATOR_ERR_STATE = -2,
    ACTUATOR_ERR_IO = -3
} ActuatorStatus_t;

/**
 * @brief 执行器设备状态。
 */
typedef enum
{
    ACTUATOR_STATE_UNINIT = 0,
    ACTUATOR_STATE_READY,
    ACTUATOR_STATE_ERROR
} ActuatorState_t;

/**
 * @brief 执行器类型。
 */
typedef enum
{
    ACTUATOR_TYPE_LED = 0,
    ACTUATOR_TYPE_RGB,
    ACTUATOR_TYPE_BEEP
} ActuatorType_t;

/**
 * @brief RGB 状态灯颜色。
 *
 * RGB 三路 GPIO 由驱动层组合，业务层只需要选择语义颜色，
 * 不需要分别控制 R/G/B 三个引脚。
 */
typedef enum
{
    RGB_COLOR_OFF = 0,
    RGB_COLOR_RED,
    RGB_COLOR_GREEN,
    RGB_COLOR_BLUE,
    RGB_COLOR_PURPLE
} RGBColor_t;

/**
 * @brief 通用执行器输出值。
 */
typedef struct
{
    uint8_t on;             /* LED/BEEP 使用：0 关闭，1 打开；RGB 为 0 时强制关闭 */
    RGBColor_t rgb_color;   /* RGB 使用：指定状态灯颜色；普通 LED/BEEP 忽略该字段 */
} ActuatorValue_t;

struct ActuatorDevice;

/**
 * @brief 执行器操作表，形参
 */
typedef struct
{
    int (*init)(struct ActuatorDevice *dev);//初始化函数，负责执行器设备的初始化工作，比如配置 GPIO 引脚、设置初始状态等。业务层在调用 Actuator_Init 时会调用这个函数来完成设备的初始化。
    int (*set)(struct ActuatorDevice *dev, const ActuatorValue_t *value);//设置函数，负责控制执行器的输出状态。业务层在需要改变执行器状态时会调用这个函数。
    int (*get)(struct ActuatorDevice *dev, ActuatorValue_t *value);//获取函数，负责读取执行器的当前输出状态。业务层在需要了解执行器状态时会调用这个函数。
    int (*deinit)(struct ActuatorDevice *dev);//反初始化函数，负责执行器设备的反初始化工作，比如复位 GPIO 引脚、关闭设备等。业务层在调用 Actuator_Deinit 时会调用这个函数来完成设备的反初始化。
} ActuatorOps_t;

/**
 * @brief 执行器设备对象。
 */
typedef struct ActuatorDevice
{
    const char *name;//字符串
		ActuatorType_t type;//设备名
    ActuatorState_t state;//设备状态
    void *private_data;//私有数据
    const ActuatorOps_t *ops;//操作表
} ActuatorDevice_t;

/**
 * @brief 初始化执行器设备。
 */
int Actuator_Init(ActuatorDevice_t *dev);

/**
 * @brief 设置执行器输出。
 */
int Actuator_Set(ActuatorDevice_t *dev, const ActuatorValue_t *value);

/**
 * @brief 读取执行器当前输出。
 */
int Actuator_Get(ActuatorDevice_t *dev, ActuatorValue_t *value);

/**
 * @brief 反初始化执行器设备。
 */
int Actuator_Deinit(ActuatorDevice_t *dev);

/**
 * @brief 设置 RGB 状态灯颜色。
 *
 * 调用前需要先执行 Actuator_Init(&g_rgb_led_actuator)。
 * color 为 RGB_COLOR_OFF 时关闭三路 RGB。
 */
int RGB_Led_SetColor(RGBColor_t color);

/**
 * @brief 读取 RGB 状态灯当前颜色。
 */
int RGB_Led_GetColor(RGBColor_t *color);

/* 系统已注册的执行器设备实例。 */
extern ActuatorDevice_t g_rgb_led_actuator;
extern ActuatorDevice_t g_led_light_actuator;
extern ActuatorDevice_t g_beep_actuator;

#endif
