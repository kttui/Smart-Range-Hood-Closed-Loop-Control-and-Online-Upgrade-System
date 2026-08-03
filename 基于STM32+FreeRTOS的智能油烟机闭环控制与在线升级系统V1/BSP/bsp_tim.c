#include "bsp_tim.h"
#include "board_pins.h"

/* ===================== TIM2_CH1 电机 PWM =====================
 * PA0 复用为 TIM2_CH1，输出给 TB6612 的 PWMA。
 * 上层 drv_motor_pwm.c 使用 0~1000 的占空比抽象，
 * 本文件只负责把比较值写入 TIM2 CCR1。
 */

static u16 s_tim2_pwm_period = 0;

void TIM2_PWM_CH1_Init(u16 arr, u16 psc)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef tim;
    TIM_OCInitTypeDef oc;

    s_tim2_pwm_period = arr;

    RCC_APB2PeriphClockCmd(MOTOR_PWM_GPIO_CLK | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    gpio.GPIO_Pin = MOTOR_PWM_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MOTOR_PWM_PORT, &gpio);

    TIM_TimeBaseStructInit(&tim);
    tim.TIM_Period = arr;
    tim.TIM_Prescaler = psc;
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &tim);

    TIM_OCStructInit(&oc);
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    oc.TIM_Pulse = 0;
    TIM_OC1Init(TIM2, &oc);

    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM2, ENABLE);

    TIM_SetCompare1(TIM2, 0);
    TIM_Cmd(TIM2, ENABLE);
}

void TIM2_PWM_CH1_SetCompare(u16 compare)
{
    if (compare > s_tim2_pwm_period)
    {
        compare = s_tim2_pwm_period;
    }

    TIM_SetCompare1(TIM2, compare);
}

void TIM2_PWM_CH1_Stop(void)
{
    TIM_SetCompare1(TIM2, 0);
}

/* ===================== TIM4 编码器测速 =====================
 * PB6 -> TIM4_CH1 -> ENCODER_B
 * PB7 -> TIM4_CH2 -> ENCODER_A
 *
 * TIM4 使用硬件编码器模式对 A/B 相脉冲计数。
 * Speed_Service 每 50ms 读取一次计数差并清零，从而计算 RPM。
 */

void TIM4_EncoderMode_Init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef tim;
    TIM_ICInitTypeDef ic;

    RCC_APB2PeriphClockCmd(ENCODER_GPIO_CLK | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

    /*
     * 内部上拉只适合短线和信号较干净的编码器。
     * 如果日志里 ea/eb 长期为 0 或抖动明显，优先检查外部上拉和接地。
     */
    gpio.GPIO_Pin = ENCODER_A_PIN | ENCODER_B_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ENCODER_PORT, &gpio);

    TIM_DeInit(TIM4);

    TIM_TimeBaseStructInit(&tim);
    tim.TIM_Period = 0xFFFF;
    tim.TIM_Prescaler = 0;
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM4, &tim);

    TIM_EncoderInterfaceConfig(TIM4,
                               TIM_EncoderMode_TI12,
                               TIM_ICPolarity_Rising,
                               TIM_ICPolarity_Rising);

    /*
     * 输入滤波用于抑制线缆抖动带来的毛刺。
     * 如果后续编码器最高转速很高但读数偏低，可适当减小 TIM_ICFilter。
     */
    TIM_ICStructInit(&ic);
    ic.TIM_Channel = TIM_Channel_1;
    ic.TIM_ICFilter = 6;
    TIM_ICInit(TIM4, &ic);

    ic.TIM_Channel = TIM_Channel_2;
    ic.TIM_ICFilter = 6;
    TIM_ICInit(TIM4, &ic);

    TIM_SetCounter(TIM4, 0);
    TIM_Cmd(TIM4, ENABLE);
}

int16_t TIM4_Encoder_GetDeltaAndReset(void)
{
    int16_t delta;

    delta = (int16_t)TIM_GetCounter(TIM4);
    TIM_SetCounter(TIM4, 0);

    return delta;
}
