#include "speed_service.h"
#include "drv_encoder.h"
#include "board_pins.h"
#include "message_def.h"
#include "log_service.h"
#include "FreeRTOS.h"
#include "semphr.h"

/* Speed_Service_Process() 每 50 ms 调用一次，10 次打印一次，即 500 ms。 */
#define SPEED_CALC_DEBUG_PRINT_PERIOD    10U

static uint8_t s_speed_calc_debug_count = 0U;

static int16_t Speed_Service_Abs16(int16_t value)
{
    if (value < 0)
    {
        return (value == -32768) ? 32767 : (int16_t)(-value);
    }

    return value;
}

static void Speed_Service_LockState(void)
{
    if (sys_data_mutex != 0)
    {
        (void)xSemaphoreTake(sys_data_mutex, portMAX_DELAY);
    }
}

static void Speed_Service_UnlockState(void)
{
    if (sys_data_mutex != 0)
    {
        xSemaphoreGive(sys_data_mutex);
    }
}

void Speed_Service_Init(void)
{
    /*
     * 测速服务只初始化编码器计数外设。
     * 它不关心当前是手动模式还是自动模式，也不直接修改 PWM，
     * 这样测速结果可以被电机控制、UI、调试日志同时复用。
     */
    Encoder_Init();
}

void Speed_Service_Process(void)
{
    int16_t delta_count;
    int16_t calculated_rpm;
    int16_t rpm;
    uint8_t encoder_a_level;
    uint8_t encoder_b_level;

    /*
     * TIM4 工作在硬件编码器模式。
     * 每 50ms 读取一次本周期计数差，并立即清零计数器，
     * 这样 current_rpm 表示最近一个采样周期的实时速度。
     */
    delta_count = Encoder_GetDeltaAndReset();
    calculated_rpm = Encoder_CalcRpm(delta_count, SPEED_SERVICE_PERIOD_MS);
    rpm = Speed_Service_Abs16(calculated_rpm);
    encoder_a_level = (GPIO_ReadInputDataBit(ENCODER_PORT, ENCODER_A_PIN) == Bit_SET) ? 1u : 0u;
    encoder_b_level = (GPIO_ReadInputDataBit(ENCODER_PORT, ENCODER_B_PIN) == Bit_SET) ? 1u : 0u;

    Speed_Service_LockState();
    g_system_state.motor.current_rpm = rpm;
    g_system_state.motor.encoder_delta = delta_count;
    g_system_state.motor.encoder_a_level = encoder_a_level;
    g_system_state.motor.encoder_b_level = encoder_b_level;
    g_system_state.motor.speed_sample_count++;
    Speed_Service_UnlockState();

    if (++s_speed_calc_debug_count >= SPEED_CALC_DEBUG_PRINT_PERIOD)
    {
        s_speed_calc_debug_count = 0U;
        LOG_INFO("ENC CALC delta=%d sample=%u cpr=%u raw_rpm=%d rpm=%d ea=%u eb=%u",
                 delta_count,
                 (unsigned int)SPEED_SERVICE_PERIOD_MS,
                 (unsigned int)ENCODER_COUNTS_PER_REV,
                 calculated_rpm,
                 rpm,
                 (unsigned int)encoder_a_level,
                 (unsigned int)encoder_b_level);
    }
}
