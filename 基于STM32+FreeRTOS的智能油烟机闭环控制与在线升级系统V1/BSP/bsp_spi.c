#include "bsp_spi.h"
#include "stm32f10x.h"
#include "board_pins.h"

static void BSP_SPI_LCD_SckHigh(void)
{
    GPIO_SetBits(LCD_SCK_PORT, LCD_SCK_PIN);
}

static void BSP_SPI_LCD_SckLow(void)
{
    GPIO_ResetBits(LCD_SCK_PORT, LCD_SCK_PIN);
}

static void BSP_SPI_LCD_MosiHigh(void)
{
    GPIO_SetBits(LCD_MOSI_PORT, LCD_MOSI_PIN);
}

static void BSP_SPI_LCD_MosiLow(void)
{
    GPIO_ResetBits(LCD_MOSI_PORT, LCD_MOSI_PIN);
}

void BSP_SPI_LCD_Init(void)
{
    GPIO_InitTypeDef gpio;

    /*
     * 软件 SPI 只需要 SCK 和 MOSI 两根线。
     * LCD 没有读操作，所以没有配置 MISO。
     */
    RCC_APB2PeriphClockCmd(LCD_SCK_GPIO_CLK | LCD_MOSI_GPIO_CLK, ENABLE);

    gpio.GPIO_Pin = LCD_SCK_PIN | LCD_MOSI_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(LCD_SCK_PORT, &gpio);

    /*
     * 空闲时 SCK/MOSI 先拉高，和商家例程保持一致。
     * ST7735 初始化过程中再按字节逐位拉低/拉高即可。
     */
    BSP_SPI_LCD_SckHigh();
    BSP_SPI_LCD_MosiHigh();
}

void BSP_SPI_LCD_WriteByte(uint8_t data)
{
    uint8_t i;

    /*
     * ST7735 的 SPI 写入顺序是 MSB first。
     * 每一位流程：
     * 1. 先把 MOSI 放成当前数据位；
     * 2. SCK 拉低再拉高，让 LCD 在时钟边沿采样。
     */
    for (i = 0; i < 8; i++)
    {
        if ((data & 0x80u) != 0u)
        {
            BSP_SPI_LCD_MosiHigh();
        }
        else
        {
            BSP_SPI_LCD_MosiLow();
        }

        BSP_SPI_LCD_SckLow();
        BSP_SPI_LCD_SckHigh();

        data <<= 1;
    }
}
