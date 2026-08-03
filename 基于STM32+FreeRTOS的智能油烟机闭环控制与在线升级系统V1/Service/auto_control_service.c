#include "auto_control_service.h"
#include "message_def.h"
#include "log_service.h"
#include "FreeRTOS.h"
#include "semphr.h"

/* ===================== 多传感器融合参数 ===================== */
#define AUTO_TEMP_BASE                 20
#define AUTO_TEMP_MAX                  60
#define AUTO_HUMI_BASE                 40
#define AUTO_HUMI_MAX                  90

/*
 * 当前 MQ2 在系统里使用 gas_percent 百分比量。
 * 你的实测空闲环境大约在 60% 附近，后期如果重新标定 MQ2，
 * 只需要调整 AUTO_GAS_BASE / AUTO_GAS_MAX，不需要改状态机代码。
 */
#define AUTO_GAS_BASE                  50
#define AUTO_GAS_MAX                   100

#define AUTO_WEIGHT_TEMP               2
#define AUTO_WEIGHT_HUMI               2
#define AUTO_WEIGHT_GAS                6
#define AUTO_WEIGHT_SUM                10

#define AUTO_TARGET_MIN_RPM            160
#define AUTO_TARGET_MAX_RPM            280

/*
 * F 使用 0~1000 的整数表示：
 * 400 等价于 0.400，250 等价于 0.250。
 * 进入阈值高于退出阈值，这就是滞回控制，可避免临界点附近反复启停。
 */
#define AUTO_ENTER_FACTOR              400
#define AUTO_EXIT_FACTOR               250

#define AUTO_MIN_RUN_MS                10000
#define AUTO_EXIT_DELAY_MS             10000
#define AUTO_MAX_ADJUST_MS             60000
#define AUTO_MIN_RUN_TICKS             (AUTO_MIN_RUN_MS / AUTO_CONTROL_SERVICE_PERIOD_MS)
#define AUTO_EXIT_DELAY_TICKS          (AUTO_EXIT_DELAY_MS / AUTO_CONTROL_SERVICE_PERIOD_MS)
#define AUTO_MAX_ADJUST_TICKS          (AUTO_MAX_ADJUST_MS / AUTO_CONTROL_SERVICE_PERIOD_MS)

typedef struct
{
    SystemMode_t mode;
    uint8_t temperature;
    uint8_t humidity;
    uint8_t gas_percent;
} AutoControlInput_t;

typedef struct
{
    uint16_t temp_factor;       /* 温度归一化结果，0~1000。 */
    uint16_t humi_factor;       /* 湿度归一化结果，0~1000。 */
    uint16_t gas_factor;        /* 气体浓度归一化结果，0~1000。 */
    uint16_t fusion_factor;     /* 综合系数 F，0~1000。 */
    int16_t target_rpm;         /* F 映射后的目标转速。 */
} AutoFusion_t;

static AutoControlState_t s_auto_state = AUTO_CONTROL_STATE_IDLE;
static uint16_t s_min_run_ticks = 0;
static uint16_t s_calm_ticks = 0;
static uint16_t s_adjust_ticks = 0;
static SystemMode_t s_last_mode = SYS_MODE_OFF;

static void Auto_Control_LockState(void)
{
    if (sys_data_mutex != 0)
    {
        (void)xSemaphoreTake(sys_data_mutex, portMAX_DELAY);
    }
}

static void Auto_Control_UnlockState(void)
{
    if (sys_data_mutex != 0)
    {
        xSemaphoreGive(sys_data_mutex);
    }
}

static uint16_t Auto_Control_Normalize(int16_t value, int16_t base, int16_t max)
{
    /*
     * 将传感器值归一化为 0~1000。
     * value <= base 认为对风量没有额外需求；
     * value >= max  认为已经达到最大影响；
     * 中间区间使用线性插值，避免 if/else 档位式突变。
     */
    if (max <= base || value <= base)
    {
        return 0;
    }

    if (value >= max)
    {
        return 1000;
    }

    return (uint16_t)(((int32_t)(value - base) * 1000L) / (max - base));
}

static void Auto_Control_ReadInput(AutoControlInput_t *input)
{
    if (input == 0)
    {
        return;
    }

    Auto_Control_LockState();
    input->mode = g_system_state.mode;
    input->temperature = g_system_state.sensor.temperature;
    input->humidity = g_system_state.sensor.humidity;
    input->gas_percent = g_system_state.sensor.gas_percent;
    Auto_Control_UnlockState();
}

static void Auto_Control_UpdateState(int16_t target_rpm, const AutoFusion_t *fusion)
{
    Auto_Control_LockState();
    g_system_state.motor.auto_target_rpm = target_rpm;
    g_system_state.motor.auto_factor = (fusion == 0) ? 0 : fusion->fusion_factor;
    g_system_state.motor.auto_state = (uint8_t)s_auto_state;
    Auto_Control_UnlockState();
}

static void Auto_Control_Reset(AutoControlState_t next_state)
{
    s_auto_state = next_state;
    s_min_run_ticks = 0;
    s_calm_ticks = 0;
    s_adjust_ticks = 0;
}

static void Auto_Control_CalcFusion(const AutoControlInput_t *input, AutoFusion_t *fusion)
{
    uint32_t weighted_sum;

    if (input == 0 || fusion == 0)
    {
        return;
    }

    /*
     * 多传感器融合算法：
     * T_factor = normalize(temperature, 20, 60)
     * H_factor = normalize(humidity,    40, 90)
     * G_factor = normalize(gas_percent, 50, 100)
     *
     * F = 0.2*T + 0.2*H + 0.6*G
     *
     * STM32F103 没有硬件 FPU，所以这里不用 float，
     * 而是把所有比例统一放大为 0~1000 的整数。
     */
    fusion->temp_factor = Auto_Control_Normalize(input->temperature,
                                                 AUTO_TEMP_BASE,
                                                 AUTO_TEMP_MAX);
    fusion->humi_factor = Auto_Control_Normalize(input->humidity,
                                                 AUTO_HUMI_BASE,
                                                 AUTO_HUMI_MAX);
    fusion->gas_factor = Auto_Control_Normalize(input->gas_percent,
                                                AUTO_GAS_BASE,
                                                AUTO_GAS_MAX);

    weighted_sum = ((uint32_t)fusion->temp_factor * AUTO_WEIGHT_TEMP) +
                   ((uint32_t)fusion->humi_factor * AUTO_WEIGHT_HUMI) +
                   ((uint32_t)fusion->gas_factor * AUTO_WEIGHT_GAS);
    fusion->fusion_factor = (uint16_t)(weighted_sum / AUTO_WEIGHT_SUM);

    /*
     * 将综合系数 F 映射为目标转速：
     * F=0.000 -> 160RPM
     * F=1.000 -> 280RPM
     */
    fusion->target_rpm = (int16_t)(AUTO_TARGET_MIN_RPM +
                                   ((uint32_t)fusion->fusion_factor *
                                    (AUTO_TARGET_MAX_RPM - AUTO_TARGET_MIN_RPM)) / 1000u);
}

static int16_t Auto_Control_ProcessState(const AutoFusion_t *fusion)
{
    int16_t target_rpm = 0;

    if (fusion == 0)
    {
        return 0;
    }

    switch (s_auto_state)
    {
    case AUTO_CONTROL_STATE_IDLE:
        /*
         * 待机：环境综合系数低，自动模式不给风机目标。
         * 一旦 F 超过进入阈值，说明温湿度/MQ2 综合判断有油烟趋势，
         * 进入自动调节状态，目标转速由 F 连续映射。
         */
        if (fusion->fusion_factor >= AUTO_ENTER_FACTOR)
        {
            Auto_Control_Reset(AUTO_CONTROL_STATE_ADJUST);
            target_rpm = fusion->target_rpm;
            LOG_INFO("AUTO adjust enter F=%d target=%d",
                     fusion->fusion_factor,
                     target_rpm);
        }
        break;

    case AUTO_CONTROL_STATE_MIN_RUN:
        /*
         * 最小运行：刚切入 AUTO 后先低速换气一段时间。
         * 这样传感器附近的空气会被带动，避免刚进入自动模式就误判为空闲。
         */
        s_min_run_ticks++;
        target_rpm = AUTO_TARGET_MIN_RPM;

        if (fusion->fusion_factor >= AUTO_ENTER_FACTOR)
        {
            Auto_Control_Reset(AUTO_CONTROL_STATE_ADJUST);
            target_rpm = fusion->target_rpm;
            LOG_INFO("AUTO min->adjust F=%d target=%d",
                     fusion->fusion_factor,
                     target_rpm);
        }
        else if (s_min_run_ticks >= AUTO_MIN_RUN_TICKS)
        {
            Auto_Control_Reset(AUTO_CONTROL_STATE_IDLE);
            target_rpm = 0;
            LOG_INFO("AUTO idle F=%d", fusion->fusion_factor);
        }
        break;

    case AUTO_CONTROL_STATE_ADJUST:
        /*
         * 自动调节：持续根据 F 计算目标转速。
         * F 低于退出阈值时不立即停机，而是累计 10s 平稳时间，
         * 这就是“延时退出”，能把残留油烟排掉，也能抑制频繁启停。
         */
        s_adjust_ticks++;
        target_rpm = fusion->target_rpm;

        if (fusion->fusion_factor <= AUTO_EXIT_FACTOR)
        {
            if (s_calm_ticks < AUTO_EXIT_DELAY_TICKS)
            {
                s_calm_ticks++;
            }
        }
        else
        {
            s_calm_ticks = 0;
        }

        if (s_calm_ticks >= AUTO_EXIT_DELAY_TICKS)
        {
            Auto_Control_Reset(AUTO_CONTROL_STATE_IDLE);
            target_rpm = 0;
            LOG_INFO("AUTO adjust exit F=%d", fusion->fusion_factor);
        }
        else if (s_adjust_ticks >= AUTO_MAX_ADJUST_TICKS)
        {
            /*
             * 单次自动调节超过 60s 后回到最小运行重新判断。
             * 如果环境仍异常，会再次进入 ADJUST；如果环境恢复，则转入 IDLE。
             */
            Auto_Control_Reset(AUTO_CONTROL_STATE_MIN_RUN);
            target_rpm = AUTO_TARGET_MIN_RPM;
            LOG_INFO("AUTO adjust timeout F=%d", fusion->fusion_factor);
        }
        break;

    default:
        Auto_Control_Reset(AUTO_CONTROL_STATE_IDLE);
        target_rpm = 0;
        break;
    }

    return target_rpm;
}

void Auto_Control_Service_Init(void)
{
    Auto_Control_Reset(AUTO_CONTROL_STATE_IDLE);
    Auto_Control_UpdateState(0, 0);
}

void Auto_Control_Service_Process(void)
{
    AutoControlInput_t input;
    AutoFusion_t fusion = {0};
    int16_t target_rpm;

    Auto_Control_ReadInput(&input);
    Auto_Control_CalcFusion(&input, &fusion);

    if (input.mode != s_last_mode)
    {
        /*
         * 每次切入 AUTO 都先进入最小运行阶段。
         * 退出 AUTO 后立即清空自动目标，避免手动/关机模式误用旧目标。
         */
        if (input.mode == SYS_MODE_AUTO)
        {
            Auto_Control_Reset(AUTO_CONTROL_STATE_MIN_RUN);
            LOG_INFO("AUTO mode enter");
        }
        else
        {
            Auto_Control_Reset(AUTO_CONTROL_STATE_IDLE);
        }

        s_last_mode = input.mode;
    }

    if (input.mode != SYS_MODE_AUTO)
    {
        Auto_Control_UpdateState(0, &fusion);
        return;
    }

    target_rpm = Auto_Control_ProcessState(&fusion);
    Auto_Control_UpdateState(target_rpm, &fusion);
}
