#include <stdio.h>
#include <string.h>
#include "drv_console.h"
#include "bsp_usart.h"

#if 1
#pragma import(__use_no_semihosting)

struct __FILE
{
    int handle;
};

FILE __stdout;

void _sys_exit(int x)
{
    (void)x;
}
#endif

void Console_Init(unsigned int baudrate)
{
    HAL_UartConfig_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.baudrate = baudrate;
    cfg.enable_rx_irq = 0;
    cfg.enable_idle_irq = 0;
    cfg.preemption_priority = 6;
    cfg.sub_priority = 0;

    HAL_UART_Init(HAL_UART_1, &cfg);
}

int fputc(int ch, FILE *f)
{
    (void)f;
		if (ch == '\n') {
        fputc('\r', f);  // ??????
    }
    HAL_UART_SendByte(HAL_UART_1, (uint8_t)ch);
    return ch;
}

