#include "sensor_service.h"
#include "drv_sensor.h"
#include "message_def.h"
#include "log_service.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "system_state.h"
/**
 * @file sensor_service.c
 * @brief 传感器业务服务。
 *
 * 本服务负责“传感器业务”：
 * 1. 读取温湿度和 MQ2 气体浓度
 * 2. 过滤异常值
 * 3. 更新 g_system_state 中的传感器数据
 * 4. 通过串口打印本轮有效采样结果
 *
 * 当前阶段只做采集和打印，不发布 LED/BEEP 等自动控制事件。
 */

/* 共享数据加锁最长等待时间，避免传感器任务被长时间阻塞。 */
#define SYS_DATA_LOCK_TIMEOUT_MS      50

/* 传感器业务层的基础合法范围过滤。 */
#define SENSOR_GAS_PERCENT_MAX      100
#define SENSOR_HUMIDITY_MAX           100
#define SENSOR_TEMPERATURE_MAX        80



/**
 * @brief 单次采样结果。
 *
 * *_valid 表示本轮采样数据通过读取和业务合法性校验。
 */
typedef struct
{
    SensorData_t data;
    uint8_t gas_valid;
    uint8_t dht11_valid;
} SensorSample_t;

/**
 * @brief 将本轮有效传感器数据写入系统共享数据区。
 *
 * 只更新本轮读取成功且通过合法性校验的字段。
 * 如果某个传感器本轮失败，保留上一次有效缓存，避免 LCD 显示突然跳成 0。
 */
static void Sensor_Service_UpdateSystemData(const SensorSample_t *sample)
{
    if (sample == NULL || sys_data_mutex == NULL)
    {
        return;
    }

    if (xSemaphoreTake(sys_data_mutex, pdMS_TO_TICKS(SYS_DATA_LOCK_TIMEOUT_MS)) == pdTRUE)
    {
        if (sample->gas_valid)
        {
            g_system_state.sensor.gas_percent = sample->data.gas_percent;
        }

        if (sample->dht11_valid)
        {
            g_system_state.sensor.temperature = sample->data.temperature;
            g_system_state.sensor.humidity = sample->data.humidity;
        }

        xSemaphoreGive(sys_data_mutex);
    }
}

/**
 * @brief 校验气体百分比是否处于业务可接受范围。
 */
static uint8_t Sensor_Service_IsGasValueValid(const SensorData_t *data)
{
    return (data != 0 && data->gas_percent <= SENSOR_GAS_PERCENT_MAX);
}

/**
 * @brief 校验 DHT11 温湿度是否处于业务可接受范围。
 */
static uint8_t Sensor_Service_IsDht11ValueValid(const SensorData_t *data)
{
    return (data != 0 &&
            data->humidity <= SENSOR_HUMIDITY_MAX &&
            data->temperature <= SENSOR_TEMPERATURE_MAX);
}

/**
 * @brief 执行一次传感器读取，并记录本轮数据有效性。
 */
static void Sensor_Service_ReadSample(SensorSample_t *sample)
{
    int ret;

    if (sample == NULL)
    {
        return;
    }

    sample->gas_valid = 0;
    sample->dht11_valid = 0;

    ret = Sensor_Read(&g_gas_sensor, &sample->data);
    if (ret == SENSOR_OK && Sensor_Service_IsGasValueValid(&sample->data))
    {
        sample->gas_valid = 1;
    }
    else if (ret == SENSOR_OK)
    {
        /* 气体浓度越界时不打印，保留上一轮有效值，避免显示误导。 */
    }
    else
    {
        /* 读取失败时本轮气体数据无效，后续只打印其他有效传感器数据。 */
    }

    ret = Sensor_Read(&g_dht11_sensor, &sample->data);
    if (ret == SENSOR_OK && Sensor_Service_IsDht11ValueValid(&sample->data))
    {
        sample->dht11_valid = 1;
    }
    else if (ret == SENSOR_OK)
    {
        /* 温湿度越界时不打印，保留上一轮有效值，避免显示误导。 */
    }
    else
    {
        /* DHT11 失败后重新初始化，下一轮继续尝试读取。 */
        Sensor_Deinit(&g_dht11_sensor);
        Sensor_Init(&g_dht11_sensor);
    }
}

/**
 * @brief 打印本轮有效采样结果。
 *
 * 只打印通过读取和合法性校验的数据；某个传感器本轮无效时，在串口中明确标记 invalid。
 */
static void Sensor_Service_PrintSample(const SensorSample_t *sample)
{
    if (sample == NULL || (!sample->gas_valid && !sample->dht11_valid))
    {
        return;
    }

    if (sample->gas_valid && sample->dht11_valid)
    {
        LOG_INFO("MQ2 Gas=%d%%, Temp=%dC, Humi=%d%%",
                 sample->data.gas_percent,
                 sample->data.temperature,
                 sample->data.humidity);
    }
    else if (sample->gas_valid)
    {
        LOG_WARN("MQ2 Gas=%d%%, Temp/Humi invalid",
                 sample->data.gas_percent);
    }
    else
    {
        LOG_WARN("MQ2 invalid, Temp=%dC, Humi=%d%%",
                 sample->data.temperature,
                 sample->data.humidity);
    }
}

/**
 * @brief 初始化传感器服务依赖的底层传感器设备。
 */
void Sensor_Service_Init(void)
{
    Sensor_Init(&g_gas_sensor);
    Sensor_Init(&g_dht11_sensor);
}




/**
 * @brief 传感器服务周期处理入口。
 *
 * 由 sensor_task 周期调用。该函数完成一次采集、缓存更新和串口打印。
 */
void Sensor_Service_Process(void)
{
    SensorSample_t sample = {0};

    Sensor_Service_ReadSample(&sample);
    Sensor_Service_UpdateSystemData(&sample);
    Sensor_Service_PrintSample(&sample);
}
