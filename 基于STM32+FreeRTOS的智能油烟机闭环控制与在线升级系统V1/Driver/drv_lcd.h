#ifndef __DRV_LCD_H
#define __DRV_LCD_H

#include <stdint.h>

#define LCD_DRV_WIDTH      128
#define LCD_DRV_HEIGHT     128

#define LCD_COLOR_BLACK    0x0000
#define LCD_COLOR_WHITE    0xFFFF
#define LCD_COLOR_RED      0xF800
#define LCD_COLOR_GREEN    0x07E0
#define LCD_COLOR_BLUE     0x001F
#define LCD_COLOR_YELLOW   0xFFE0
#define LCD_COLOR_CYAN     0x07FF
#define LCD_COLOR_MAGENTA  0xF81F
#define LCD_COLOR_GRAY     0x8410
#define LCD_COLOR_DARK     0x2104

void Drv_Lcd_Init(void);
void Drv_Lcd_Clear(uint16_t color);
void Drv_Lcd_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void Drv_Lcd_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
void Drv_Lcd_DrawChar(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t bg_color);
void Drv_Lcd_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color);

#endif
