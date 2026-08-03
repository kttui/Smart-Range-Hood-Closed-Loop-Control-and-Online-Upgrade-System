#ifndef __BOARD_PINS_H__
#define __BOARD_PINS_H__

/*
 * board_pins.h
 *
 * 这个文件只做一件事：集中描述当前 PCB 的 MCU 引脚分配。
 * Driver/BSP/Service 不直接写死 GPIOA、GPIO_Pin_x，而是引用这里的宏。
 * 后续如果改板或飞线换引脚，优先只改这个文件。
 */

/* ===================== RGB 状态灯 =====================
 * 三路 RGB 状态灯均为低电平有效：
 * GPIO 输出 0 -> 对应颜色点亮
 * GPIO 输出 1 -> 对应颜色熄灭
 */
#define LED_R_PORT                 GPIOC
#define LED_R_PIN                  GPIO_Pin_7
#define LED_R_GPIO_CLK             RCC_APB2Periph_GPIOC
#define LED_R_ACTIVE_LEVEL         0

#define LED_G_PORT                 GPIOC
#define LED_G_PIN                  GPIO_Pin_8
#define LED_G_GPIO_CLK             RCC_APB2Periph_GPIOC
#define LED_G_ACTIVE_LEVEL         0

#define LED_B_PORT                 GPIOC
#define LED_B_PIN                  GPIO_Pin_9
#define LED_B_GPIO_CLK             RCC_APB2Periph_GPIOC
#define LED_B_ACTIVE_LEVEL         0

/* ===================== 烟机照明灯 =====================
 * 单独的照明灯为高电平有效。
 */
#define LED_LIGHT_PORT             GPIOC
#define LED_LIGHT_PIN              GPIO_Pin_6
#define LED_LIGHT_GPIO_CLK         RCC_APB2Periph_GPIOC
#define LED_LIGHT_ACTIVE_LEVEL     1

/* ===================== 蜂鸣器 =====================
 * 按键短按时短鸣，模拟真实烟机按键反馈。
 */
#define BEEP_PORT                  GPIOB
#define BEEP_PIN                   GPIO_Pin_0
#define BEEP_GPIO_CLK              RCC_APB2Periph_GPIOB
#define BEEP_ACTIVE_LEVEL          1

/* ===================== 电机 TB6612 =====================
 * MOTOR_PWM -> PA0 -> TIM2_CH1
 * MOTOR_IN1/IN2 控制方向
 * MOTOR_STBY 拉高后 TB6612 才退出待机
 */
#define MOTOR_PWM_PORT             GPIOA
#define MOTOR_PWM_PIN              GPIO_Pin_0
#define MOTOR_PWM_GPIO_CLK         RCC_APB2Periph_GPIOA

#define MOTOR_IN1_PORT             GPIOB
#define MOTOR_IN1_PIN              GPIO_Pin_12
#define MOTOR_IN1_GPIO_CLK         RCC_APB2Periph_GPIOB

#define MOTOR_IN2_PORT             GPIOB
#define MOTOR_IN2_PIN              GPIO_Pin_13
#define MOTOR_IN2_GPIO_CLK         RCC_APB2Periph_GPIOB

#define MOTOR_STBY_PORT            GPIOB
#define MOTOR_STBY_PIN             GPIO_Pin_14
#define MOTOR_STBY_GPIO_CLK        RCC_APB2Periph_GPIOB

/* ===================== 电机编码器 =====================
 * ENCODER_A -> PB7 -> TIM4_CH2
 * ENCODER_B -> PB6 -> TIM4_CH1
 *
 * 注意：
 * 1. PB6/PB7 也是 STM32F103 默认 I2C1_SCL/SDA。
 *    当前工程已经把 PB6/PB7 分配给编码器，不要再同时接 SHT30/I2C1。
 * 2. 如果编码器 A/B 是开漏输出，建议外接 4.7k~10k 上拉到 3.3V。
 * 3. ENCODER_COUNTS_PER_REV 表示 TIM4 计数器转一圈累计的计数值，
 *    需要根据编码器线数、倍频方式和减速比实测修正。
 */
#define ENCODER_PORT               GPIOB
#define ENCODER_A_PIN              GPIO_Pin_7
#define ENCODER_B_PIN              GPIO_Pin_6
#define ENCODER_GPIO_CLK           RCC_APB2Periph_GPIOB
#define ENCODER_COUNTS_PER_REV     2600

/* ===================== LCD ST7735 软件 SPI =====================
 * 当前 LCD 使用 IO 模拟 SPI，不占用 STM32 硬件 SPI 外设。
 *
 * CS   -> PA4，片选，低电平选中 LCD
 * SCK  -> PA5，软件 SPI 时钟
 * DC   -> PA6，命令/数据选择，也常叫 A0/RS
 * MOSI -> PA7，软件 SPI 数据线，只写屏幕
 * RST  -> PC4，LCD 硬复位
 *
 * LCD 背光已经直接接 3.3V，程序不再占用背光 GPIO。
 */
#define LCD_CS_PORT                GPIOA
#define LCD_CS_PIN                 GPIO_Pin_4
#define LCD_CS_GPIO_CLK            RCC_APB2Periph_GPIOA

#define LCD_SCK_PORT               GPIOA
#define LCD_SCK_PIN                GPIO_Pin_5
#define LCD_SCK_GPIO_CLK           RCC_APB2Periph_GPIOA

#define LCD_DC_PORT                GPIOA
#define LCD_DC_PIN                 GPIO_Pin_6
#define LCD_DC_GPIO_CLK            RCC_APB2Periph_GPIOA

#define LCD_MOSI_PORT              GPIOA
#define LCD_MOSI_PIN               GPIO_Pin_7
#define LCD_MOSI_GPIO_CLK          RCC_APB2Periph_GPIOA

#define LCD_RST_PORT               GPIOC
#define LCD_RST_PIN                GPIO_Pin_4
#define LCD_RST_GPIO_CLK           RCC_APB2Periph_GPIOC

/* ===================== 自锁电源 =====================
 * PWR_HOLD 拉高后保持系统 12V_SYS/后级电源。
 * KEY_POWER 按下为低电平。
 */
#define PWR_HOLD_PORT              GPIOC
#define PWR_HOLD_PIN               GPIO_Pin_10
#define PWR_HOLD_GPIO_CLK          RCC_APB2Periph_GPIOC

#define KEY_POWER_PORT             GPIOC
#define KEY_POWER_PIN              GPIO_Pin_13
#define KEY_POWER_GPIO_CLK         RCC_APB2Periph_GPIOC
#define KEY_POWER_ACTIVE_LEVEL     0

/* ===================== 面板按键 =====================
 * 按键均为低电平有效。
 */
#define KEY_MODE_PORT              GPIOB
#define KEY_MODE_PIN               GPIO_Pin_10
#define KEY_MODE_GPIO_CLK          RCC_APB2Periph_GPIOB
#define KEY_MODE_ACTIVE_LEVEL      0

#define KEY_SPEED_PORT             GPIOB
#define KEY_SPEED_PIN              GPIO_Pin_11
#define KEY_SPEED_GPIO_CLK         RCC_APB2Periph_GPIOB
#define KEY_SPEED_ACTIVE_LEVEL     0

#define KEY_LIGHT_PORT             GPIOC
#define KEY_LIGHT_PIN              GPIO_Pin_5
#define KEY_LIGHT_GPIO_CLK         RCC_APB2Periph_GPIOC
#define KEY_LIGHT_ACTIVE_LEVEL     0

#define KEY_UPGRADE_PORT           GPIOC
#define KEY_UPGRADE_PIN            GPIO_Pin_11
#define KEY_UPGRADE_GPIO_CLK       RCC_APB2Periph_GPIOC
#define KEY_UPGRADE_ACTIVE_LEVEL   0

/* ===================== 传感器 ===================== */
#define DHT11_PORT                 GPIOC
#define DHT11_PIN                  GPIO_Pin_0
#define DHT11_GPIO_CLK             RCC_APB2Periph_GPIOC

#define GAS_ADC_PORT               GPIOA
#define GAS_ADC_PIN                GPIO_Pin_1
#define GAS_ADC_CHANNEL            ADC_Channel_1
#define GAS_ADC_GPIO_CLK           RCC_APB2Periph_GPIOA
#define GAS_ADC_SAMPLES            10

/* ===================== 串口 =====================
 * USART1：调试串口，连接串口助手。
 * USART2：预留给 ESP32 AT/在线升级通信，当前业务还没有接入。
 */
#define USART1_TX_PORT             GPIOA
#define USART1_TX_PIN              GPIO_Pin_9
#define USART1_RX_PORT             GPIOA
#define USART1_RX_PIN              GPIO_Pin_10
#define USART1_GPIO_CLK            RCC_APB2Periph_GPIOA
#define USART1_CLK                 RCC_APB2Periph_USART1

#define USART2_TX_PORT             GPIOA
#define USART2_TX_PIN              GPIO_Pin_2
#define USART2_RX_PORT             GPIOA
#define USART2_RX_PIN              GPIO_Pin_3
#define USART2_GPIO_CLK            RCC_APB2Periph_GPIOA
#define USART2_CLK                 RCC_APB1Periph_USART2

#endif /* __BOARD_PINS_H__ */
