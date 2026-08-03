#ifndef __HAL_USART_H__
#define __HAL_USART_H__

#include "stm32f10x.h"
#include <stdint.h>

typedef enum
{
    HAL_UART_1 = 0,
    HAL_UART_2,
    HAL_UART_MAX
} HAL_UartId_t;

typedef struct
{
    uint32_t baudrate;
    uint8_t enable_rx_irq;
    uint8_t enable_idle_irq;
    uint8_t preemption_priority;
    uint8_t sub_priority;
} HAL_UartConfig_t;

typedef void (*HAL_UartRxCallback_t)(uint8_t data);
/*
 * IDLE 回调用 void * 承接 ISR 唤醒参数，避免 hal_usart.h 暴露 FreeRTOS 类型。
 *
 * 原因：
 * FreeRTOSConfig.h 会包含 hal_usart.h，如果这里再包含 FreeRTOS.h 并使用 BaseType_t，
 * 编译 FreeRTOS 内核时会形成包含顺序问题，导致 BaseType_t 尚未定义。
 */
typedef void (*HAL_UartIdleCallback_t)(void *pxHigherPriorityTaskWoken);

void HAL_UART_Init(HAL_UartId_t uart_id, const HAL_UartConfig_t *cfg);
void HAL_UART_SendByte(HAL_UartId_t uart_id, uint8_t data);
void HAL_UART_SendBuffer(HAL_UartId_t uart_id, const uint8_t *buf, uint16_t len);
void HAL_UART_SendString(HAL_UartId_t uart_id, const char *str);

void HAL_UART_RegisterRxCallback(HAL_UartId_t uart_id, HAL_UartRxCallback_t cb);
void HAL_UART_RegisterIdleCallback(HAL_UartId_t uart_id, HAL_UartIdleCallback_t cb);
void HAL_UART_IRQHandler(HAL_UartId_t uart_id);

#endif
