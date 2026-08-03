#ifndef __SENSOR_SERVICE_H
#define __SENSOR_SERVICE_H

#include <stdint.h>

/**
 * @brief 初始化传感器服务。
 *
 * 由 sensor_task 启动时调用一次。
 */
void Sensor_Service_Init(void);

/**
 * @brief 执行一次传感器业务处理。
 *
 * 由 sensor_task 周期调用，内部完成采集、异常过滤、共享数据更新和串口打印。
 */
void Sensor_Service_Process(void);

#endif
