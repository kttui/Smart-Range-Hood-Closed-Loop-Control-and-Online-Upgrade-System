#include "bsp_power.h"
#include "board_pins.h"

/*
 * 上电早期还没有启动 FreeRTOS 和 delay_init，这里使用短忙等延时。
 * 只用于电源按键去抖和等待松手，不参与正常业务计时。
 */
static void BSP_Power_DelayMs(uint32_t ms)
{
    uint32_t i;

    while (ms--)
    {
        for (i = 0; i < 7200; i++)
        {
            __NOP();
        }
    }
}

void BSP_Power_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(PWR_HOLD_GPIO_CLK | KEY_POWER_GPIO_CLK, ENABLE);

    gpio.GPIO_Pin = KEY_POWER_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(KEY_POWER_PORT, &gpio);

    gpio.GPIO_Pin = PWR_HOLD_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PWR_HOLD_PORT, &gpio);

    /* 默认不保持电源，只有确认按键开机后才拉高自锁脚。 */
    BSP_Power_HoldOff();
}

void BSP_Power_HoldOn(void)
{
    GPIO_SetBits(PWR_HOLD_PORT, PWR_HOLD_PIN);
}

void BSP_Power_HoldOff(void)
{
    GPIO_ResetBits(PWR_HOLD_PORT, PWR_HOLD_PIN);
}

void BSP_Power_ResumeHold(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(PWR_HOLD_GPIO_CLK | KEY_POWER_GPIO_CLK, ENABLE);

    gpio.GPIO_Pin = KEY_POWER_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(KEY_POWER_PORT, &gpio);

    gpio.GPIO_Pin = PWR_HOLD_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PWR_HOLD_PORT, &gpio);
    BSP_Power_HoldOn();
}

uint8_t BSP_Power_KeyIsPressed(void)
{
    return (GPIO_ReadInputDataBit(KEY_POWER_PORT, KEY_POWER_PIN) == Bit_RESET) ? 1 : 0;
}

uint8_t BSP_Power_BootCheckAndHold(void)
{
    BSP_Power_Init();

    /*
     * 关键逻辑：
     * 只有检测到电源键确实被按下，才允许拉高 PWR_HOLD。
     * 如果不是按键开机，而是掉电过程中的异常复位，就不再自锁。
     */
    BSP_Power_DelayMs(30);

    if (BSP_Power_KeyIsPressed())
    {
        BSP_Power_HoldOn();
        return 1;
    }

    BSP_Power_HoldOff();
    return 0;
}

uint8_t BSP_Power_WaitKeyPressAndHold(uint32_t timeout_ms)
{
    uint32_t waited_ms = 0;

    /*
     * 下载器给 3.3V/5V 时，MCU 可能在没有按电源键的情况下先启动。
     * 此时不能直接停在死循环，否则后面再按自锁键也不会拉高 PWR_HOLD。
     * timeout_ms 为 0 表示一直等待，适合下载调试场景。
     */
    while (1)
    {
        if (BSP_Power_KeyIsPressed())
        {
            BSP_Power_DelayMs(30);
            if (BSP_Power_KeyIsPressed())
            {
                BSP_Power_HoldOn();
                return 1;
            }
        }

        if (timeout_ms != 0 && waited_ms >= timeout_ms)
        {
            BSP_Power_HoldOff();
            return 0;
        }

        BSP_Power_DelayMs(10);
        if (timeout_ms != 0)
        {
            waited_ms += 10;
        }
    }
}

uint8_t BSP_Power_WaitKeyRelease(uint32_t timeout_ms)
{
    uint32_t waited_ms = 0;

    /*
     * 等待用户松开电源键。
     * 防止开机时一直按着，进入系统后马上被识别成长按关机。
     * 若按键电路在自锁后仍保持低电平，不能在启动前无限等待，否则系统永远进不了 FreeRTOS。
     */
    while (BSP_Power_KeyIsPressed())
    {
        if (waited_ms >= timeout_ms)
        {
            return 0;
        }

        BSP_Power_DelayMs(10);
        waited_ms += 10;
    }

    return 1;
}
