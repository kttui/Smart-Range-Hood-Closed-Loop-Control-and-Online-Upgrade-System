#ifndef __BSP_SPI_H
#define __BSP_SPI_H

#include <stdint.h>

/*
 * 本文件只封装 LCD 使用的软件 SPI。
 *
 * 注意：
 * 1. 这里不是 STM32 的硬件 SPI 外设，只是用 GPIO 翻转模拟 SCK/MOSI。
 * 2. LCD 驱动层不直接操作 PA5/PA7，而是调用这里的 BSP_SPI_LCD_WriteByte()。
 * 3. 以后如果要改成硬件 SPI，只需要替换本文件实现，drv_lcd/ui_service 不需要跟着改。
 */
void BSP_SPI_LCD_Init(void);
void BSP_SPI_LCD_WriteByte(uint8_t data);

#endif
