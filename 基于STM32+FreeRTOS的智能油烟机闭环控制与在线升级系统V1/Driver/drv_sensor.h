#ifndef __DRV_SENSOR_H
#define __DRV_SENSOR_H

/**
 * @file drv_sensor.h
 * @brief 传感器驱动抽象接口。
 *
 * Driver 层只负责和具体硬件交互并返回原始采集结果。
 * 数据过滤、系统缓存更新和事件上报应放在 sensor_service。
 */

#include <stdint.h>

/**
 * @brief 传感器驱动返回值。
 */
typedef enum
{
    SENSOR_OK = 0,         // 成功
    SENSOR_ERR_PARAM = -1,   // 参数错误
    SENSOR_ERR_STATE = -2,      // 设备状态不合法
    SENSOR_ERR_TIMEOUT = -3,        // 设备响应超时
    SENSOR_ERR_CHECKSUM = -4,       // 数据校验失败
    SENSOR_ERR_IO = -5      // 硬件交互失败
} SensorStatus_t;

/**
 * @brief 传感器设备状态。
 */
typedef enum
{
    SENSOR_STATE_UNINIT = 0,        // 未初始化
    SENSOR_STATE_READY,             // 已就绪
    SENSOR_STATE_ERROR              // 错误状态
} SensorState_t;

/**
 * @brief 传感器类型。
 */
typedef enum
{
    SENSOR_TYPE_DHT11,        // 温湿度传感器
    SENSOR_TYPE_GAS_SENSOR      // 气体传感器
} SensorType_t;

/**
 * @brief 通用传感器数据结构。不管是什么传感器，数据都存放在这里。对于不相关的字段，驱动层可以设置为0。
 * 业务层根据传感器类型来解析这些数据。
 */
typedef struct
{
    uint8_t gas_percent;      // 气体浓度百分比，0-100
    uint8_t temperature;        // 温度值，单位摄氏度
    uint8_t humidity;       // 湿度值，单位百分比
} SensorData_t;

struct SensorDevice;

/**
 * @brief 传感器操作表。类似java的接口，定义了传感器设备必须实现的功能函数指针。
 * 每个具体传感器设备实例会有一个对应的操作表，指向该设备的功能函数实现。
 */
typedef struct
{
    int (*init)(struct SensorDevice *dev);      // 初始化函数，负责配置硬件并准备就绪 函数指针，指向具体的初始化函数
    int (*read)(struct SensorDevice *dev, SensorData_t *data);      // 读取函数，负责采集数据并填充到 SensorData_t 结构中 函数指针，指向具体的读取函数
    int (*deinit)(struct SensorDevice *dev);      // 反初始化函数，负责释放资源 函数指针，指向具体的反初始化函数
} SensorOps_t;

/**
 * @brief 传感器设备对象。类似基类，包含了传感器的通用属性和一个指向操作表的指针。每个具体传感器设备实例都会是这个结构的一个对象，并且有自己的操作表实现。
 */
typedef struct SensorDevice
{
    const char *name;          // 设备名称，便于调试和日志记录
    SensorType_t type;      // 传感器类型，业务层根据这个字段来区分不同的传感器设备
    SensorState_t state;        // 设备状态，驱动层负责维护这个字段的正确性，业务层根据这个字段来判断设备是否可用
    void *private_data;     // 私有数据指针，驱动层可以在这里存放设备相关的上下文信息，比如GPIO配置、校准数据等。业务层不应该直接访问这个字段。
    const SensorOps_t *ops;     // 操作表指针，指向该设备的功能函数实现。通过这个指针，业务层可以调用设备的功能函数，而不需要关心具体的实现细节。
} SensorDevice_t;

/**
 * @brief 初始化传感器设备。
 */
int Sensor_Init(SensorDevice_t *dev);

/**
 * @brief 读取传感器数据。
 */
int Sensor_Read(SensorDevice_t *dev, SensorData_t *data);

/**
 * @brief 反初始化传感器设备。
 */
int Sensor_Deinit(SensorDevice_t *dev);

/* 系统已注册的传感器设备实例。 */
extern SensorDevice_t g_gas_sensor;
extern SensorDevice_t g_dht11_sensor;

#endif
