#include "bsp_sys.h"
#include "bsp_usart.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "board_pins.h"

/**
 * @file hal_usart.c
 * @brief USART 硬件抽象层。
 *
 * HAL 层只负责串口时钟、GPIO、NVIC、中断标志和字节收发。
 * ESP32 的 AT 命令、接收路由、云端帧唤醒等逻辑放在 drv_esp32.c。
 */

typedef enum
{
    HAL_UART_BUS_APB1 = 0,//STM32F103 的 USART2 和 USART3 在 APB1 总线上
    HAL_UART_BUS_APB2
} HAL_UartBus_t;

typedef struct
{
    USART_TypeDef *instance;
    uint32_t usart_clk;
    GPIO_TypeDef *tx_port;
    uint16_t tx_pin;
    GPIO_TypeDef *rx_port;
    uint16_t rx_pin;
    uint32_t gpio_clk;
    IRQn_Type iqrn;
    HAL_UartBus_t bus;
} HAL_UartMap_t;

static const HAL_UartMap_t hal_uart_map[HAL_UART_MAX] = {
    [HAL_UART_1] = {
        .instance = USART1,
        .usart_clk = USART1_CLK,
        .tx_port = USART1_TX_PORT,
        .tx_pin = USART1_TX_PIN,
        .rx_port = USART1_RX_PORT,
        .rx_pin = USART1_RX_PIN,
        .gpio_clk = USART1_GPIO_CLK,
        .iqrn = USART1_IRQn,
        .bus = HAL_UART_BUS_APB2
    },
    [HAL_UART_2] = {
        .instance = USART2,
        .usart_clk = USART2_CLK,
        .tx_port = USART2_TX_PORT,
        .tx_pin = USART2_TX_PIN,
        .rx_port = USART2_RX_PORT,
        .rx_pin = USART2_RX_PIN,
        .gpio_clk = USART2_GPIO_CLK,
        .iqrn = USART2_IRQn,
        .bus = HAL_UART_BUS_APB1
    }
};

static HAL_UartRxCallback_t uart_rx_callback[HAL_UART_MAX] = {0};
static HAL_UartIdleCallback_t uart_idle_callback[HAL_UART_MAX] = {0};

static uint8_t HAL_UART_IsValidId(HAL_UartId_t uart_id)
{
    return (uart_id < HAL_UART_MAX) ? 1 : 0;
}

static void HAL_UART_EnableClock(HAL_UartId_t uart_id)
{
    RCC_APB2PeriphClockCmd(hal_uart_map[uart_id].gpio_clk, ENABLE);

    if (hal_uart_map[uart_id].bus == HAL_UART_BUS_APB2)
    {
        RCC_APB2PeriphClockCmd(hal_uart_map[uart_id].usart_clk, ENABLE);
    }
    else
    {
        RCC_APB1PeriphClockCmd(hal_uart_map[uart_id].usart_clk, ENABLE);
    }
}

void HAL_UART_Init(HAL_UartId_t uart_id, const HAL_UartConfig_t *cfg)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef nvic;

    if (HAL_UART_IsValidId(uart_id) == 0 || cfg == 0)
    {
        return;
    }

    HAL_UART_EnableClock(uart_id);

    gpio.GPIO_Pin = hal_uart_map[uart_id].tx_pin;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(hal_uart_map[uart_id].tx_port, &gpio);

    gpio.GPIO_Pin = hal_uart_map[uart_id].rx_pin;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(hal_uart_map[uart_id].rx_port, &gpio);

    usart.USART_BaudRate = cfg->baudrate;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(hal_uart_map[uart_id].instance, &usart);

    USART_ITConfig(hal_uart_map[uart_id].instance,
                   USART_IT_RXNE,
                   cfg->enable_rx_irq ? ENABLE : DISABLE);
    USART_ITConfig(hal_uart_map[uart_id].instance,
                   USART_IT_IDLE,
                   cfg->enable_idle_irq ? ENABLE : DISABLE);

    nvic.NVIC_IRQChannel = hal_uart_map[uart_id].iqrn;
    nvic.NVIC_IRQChannelPreemptionPriority = cfg->preemption_priority;
    nvic.NVIC_IRQChannelSubPriority = cfg->sub_priority;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    USART_Cmd(hal_uart_map[uart_id].instance, ENABLE);
}

void HAL_UART_SendByte(HAL_UartId_t uart_id, uint8_t data)
{
    if (HAL_UART_IsValidId(uart_id) == 0)
    {
        return;
    }

    while (USART_GetFlagStatus(hal_uart_map[uart_id].instance, USART_FLAG_TXE) == RESET)
    {
    }
    USART_SendData(hal_uart_map[uart_id].instance, data);
}

void HAL_UART_SendBuffer(HAL_UartId_t uart_id, const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    if (buf == 0)
    {
        return;
    }

    for (i = 0; i < len; i++)
    {
        HAL_UART_SendByte(uart_id, buf[i]);
    }
}

void HAL_UART_SendString(HAL_UartId_t uart_id, const char *str)
{
    if (str == 0)
    {
        return;
    }

    while (*str)
    {
        HAL_UART_SendByte(uart_id, (uint8_t)(*str));
        str++;
    }
}

void HAL_UART_RegisterRxCallback(HAL_UartId_t uart_id, HAL_UartRxCallback_t cb)
{
    if (HAL_UART_IsValidId(uart_id) == 0)
    {
        return;
    }

    uart_rx_callback[uart_id] = cb;
}

void HAL_UART_RegisterIdleCallback(HAL_UartId_t uart_id, HAL_UartIdleCallback_t cb)
{
    if (HAL_UART_IsValidId(uart_id) == 0)
    {
        return;
    }

    uart_idle_callback[uart_id] = cb;
}

void HAL_UART_IRQHandler(HAL_UartId_t uart_id)
{
    USART_TypeDef *uart;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    volatile uint32_t clear_tmp;

    if (HAL_UART_IsValidId(uart_id) == 0)
    {
        return;
    }

    uart = hal_uart_map[uart_id].instance;

    /*
     * ORE（Overrun Error）说明上一字节未及时读走。
     * 读取 SR 再读取 DR 可以清除该错误状态。
     */
    if (USART_GetFlagStatus(uart, USART_FLAG_ORE) != RESET)
    {
        clear_tmp = uart->SR;
        clear_tmp = uart->DR;
        (void)clear_tmp;
    }

    if (USART_GetITStatus(uart, USART_IT_RXNE) != RESET)
    {
        uint8_t data = (uint8_t)USART_ReceiveData(uart);
        if (uart_rx_callback[uart_id] != 0)
        {
            uart_rx_callback[uart_id](data);
        }
    }

    /*
     * IDLE 表示一段串口数据已经结束。
     * HAL 只清硬件标志并回调上层，具体帧缓冲和业务唤醒由驱动层决定。
     */
    if (USART_GetITStatus(uart, USART_IT_IDLE) != RESET)
    {
        clear_tmp = uart->SR;
        clear_tmp = uart->DR;
        (void)clear_tmp;

        if (uart_idle_callback[uart_id] != 0)
        {
            uart_idle_callback[uart_id](&xHigherPriorityTaskWoken);
        }

        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(HAL_UART_1);
}

void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(HAL_UART_2);
}
