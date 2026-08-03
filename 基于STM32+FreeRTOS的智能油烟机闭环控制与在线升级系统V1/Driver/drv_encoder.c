#include "drv_encoder.h"
#include "bsp_tim.h"
#include "board_pins.h"

void Encoder_Init(void)
{
    /*
     * 编码器使用 TIM4 的硬件编码器模式计数。
     * Driver 层只提供“周期内计数差”和 RPM 换算，不决定目标速度。
     */
    TIM4_EncoderMode_Init();
}

int16_t Encoder_GetDeltaAndReset(void)
{
    return TIM4_Encoder_GetDeltaAndReset();
}

int16_t Encoder_CalcRpm(int16_t delta_count, uint16_t sample_ms)
{
    int32_t rpm;

    if (sample_ms == 0 || ENCODER_COUNTS_PER_REV == 0)
    {
        return 0;
    }

    /*
     * ENCODER_COUNTS_PER_REV 是 TIM4 计数器转一圈累计的计数值。
     * RPM = 周期计数差 * 60000 / (每圈计数 * 采样周期ms)
     */
    rpm = ((int32_t)delta_count * 60000L) /
          ((int32_t)ENCODER_COUNTS_PER_REV * (int32_t)sample_ms);

    if (rpm > 32767)
    {
        rpm = 32767;
    }
    else if (rpm < -32768)
    {
        rpm = -32768;
    }

    return (int16_t)rpm;
}
