#include "anti_backflow_service.h"
#include "message_def.h"
#include "log_service.h"
#include "FreeRTOS.h"
#include "semphr.h"

/*
 * 防回流核心不是硬件复杂，而是状态机：
 *
 * 1. 动态阈值切换：正常阈值、回流阈值、释放阈值分开。
 * 2. 滞回控制：进入阈值高，退出阈值低，避免浓度在临界点附近来回跳。
 * 3. 延时退出：达到释放条件后持续 10s 才退出，避免短暂波动误停机。
 */
#define BACKFLOW_NORMAL_THRESHOLD       40
#define BACKFLOW_ACTIVE_THRESHOLD       55
#define BACKFLOW_RELEASE_THRESHOLD      30

#define BACKFLOW_ACTIVE_RPM             260
#define BACKFLOW_RECOVERY_RPM           180

#define BACKFLOW_RELEASE_DELAY_MS       10000
#define BACKFLOW_RELEASE_TICKS          (BACKFLOW_RELEASE_DELAY_MS / ANTI_BACKFLOW_SERVICE_PERIOD_MS)

typedef struct
{
    SystemMode_t mode;
    uint8_t gas_percent;
} BackflowInput_t;

static BackflowState_t s_backflow_state = BACKFLOW_IDLE;
static uint16_t s_release_ticks = 0;
static SystemMode_t s_last_mode = SYS_MODE_OFF;

static void Anti_Backflow_LockState(void)
{
    if (sys_data_mutex != 0)
    {
        (void)xSemaphoreTake(sys_data_mutex, portMAX_DELAY);
    }
}

static void Anti_Backflow_UnlockState(void)
{
    if (sys_data_mutex != 0)
    {
        xSemaphoreGive(sys_data_mutex);
    }
}

static const char *Anti_Backflow_StateText(BackflowState_t state)
{
    switch (state)
    {
    case BACKFLOW_IDLE:
        return "IDLE";
    case BACKFLOW_MONITORING:
        return "MON";
    case BACKFLOW_ACTIVE:
        return "ACT";
    case BACKFLOW_RECOVERY:
        return "REC";
    default:
        return "UNK";
    }
}

static void Anti_Backflow_ReadInput(BackflowInput_t *input)
{
    if (input == 0)
    {
        return;
    }

    Anti_Backflow_LockState();
    input->mode = g_system_state.mode;
    input->gas_percent = g_system_state.sensor.gas_percent;
    Anti_Backflow_UnlockState();
}

static void Anti_Backflow_UpdateState(uint8_t active, int16_t target_rpm)
{
    Anti_Backflow_LockState();
    g_system_state.motor.backflow_active = active;
    g_system_state.motor.backflow_target_rpm = target_rpm;
    g_system_state.motor.backflow_state = (uint8_t)s_backflow_state;
    Anti_Backflow_UnlockState();
}

static void Anti_Backflow_Reset(BackflowState_t next_state)
{
    s_backflow_state = next_state;
    s_release_ticks = 0;
}

static uint8_t Anti_Backflow_ReleaseStable(uint8_t gas_percent)
{
    /*
     * release_threshold 以下持续 10s 才认为回流风险解除。
     * 只低于一次不算恢复，必须连续稳定，防止风机刚降速时浓度反弹。
     */
    if (gas_percent < BACKFLOW_RELEASE_THRESHOLD)
    {
        if (s_release_ticks < BACKFLOW_RELEASE_TICKS)
        {
            s_release_ticks++;
        }
    }
    else
    {
        s_release_ticks = 0;
    }

    return (s_release_ticks >= BACKFLOW_RELEASE_TICKS) ? 1u : 0u;
}

void Anti_Backflow_Service_Init(void)
{
    Anti_Backflow_Reset(BACKFLOW_IDLE);
    Anti_Backflow_UpdateState(0, 0);
}

void Anti_Backflow_Service_Process(void)
{
    BackflowInput_t input;
    uint8_t active = 0;
    int16_t target_rpm = 0;
    BackflowState_t old_state;

    Anti_Backflow_ReadInput(&input);

    if (input.mode != s_last_mode)
    {
        /*
         * 防回流只在自动模式下工作。
         * 手动模式由用户明确控制风量，关机/升级模式则必须清空防回流目标。
         */
        if (input.mode != SYS_MODE_AUTO)
        {
            Anti_Backflow_Reset(BACKFLOW_IDLE);
        }

        s_last_mode = input.mode;
    }

    if (input.mode != SYS_MODE_AUTO)
    {
        Anti_Backflow_UpdateState(0, 0);
        return;
    }

    old_state = s_backflow_state;

    switch (s_backflow_state)
    {
    case BACKFLOW_IDLE:
        /*
         * IDLE：正常环境，防回流不接管目标转速。
         * 一旦气体浓度超过 normal_threshold，进入监测状态，
         * 但此时还不立刻强制拉高风机，避免普通油烟轻微波动就触发。
         */
        if (input.gas_percent > BACKFLOW_NORMAL_THRESHOLD)
        {
            Anti_Backflow_Reset(BACKFLOW_MONITORING);
        }
        break;

    case BACKFLOW_MONITORING:
        /*
         * MONITORING：已发现浓度偏高，持续观察。
         * 超过 backflow_threshold 才进入 ACTIVE，
         * 低于 release_threshold 且稳定 10s 才回到 IDLE。
         */
        if (input.gas_percent > BACKFLOW_ACTIVE_THRESHOLD)
        {
            Anti_Backflow_Reset(BACKFLOW_ACTIVE);
        }
        else if (Anti_Backflow_ReleaseStable(input.gas_percent))
        {
            Anti_Backflow_Reset(BACKFLOW_IDLE);
        }
        break;

    case BACKFLOW_ACTIVE:
        /*
         * ACTIVE：判定存在回流/高浓度风险。
         * 这里给 motor_control_service 一个较高的兜底目标转速，
         * 最终 PWM 仍由电机控制任务统一输出。
         */
        active = 1;
        target_rpm = BACKFLOW_ACTIVE_RPM;

        if (input.gas_percent < BACKFLOW_RELEASE_THRESHOLD)
        {
            /*
             * 浓度第一次跌破 release_threshold 时，不直接退出 ACTIVE。
             * 先进入 RECOVERY，用较低转速继续排残留气体，并在 RECOVERY 中累计 10s。
             */
            Anti_Backflow_Reset(BACKFLOW_RECOVERY);
        }
        break;

    case BACKFLOW_RECOVERY:
        /*
         * RECOVERY：浓度已经跌破释放阈值，但还没有满足“持续 10s”。
         * 先保持较低恢复转速继续排残留气体；如果再次超过高阈值，
         * 马上回到 ACTIVE。
         */
        active = 1;
        target_rpm = BACKFLOW_RECOVERY_RPM;

        if (input.gas_percent > BACKFLOW_ACTIVE_THRESHOLD)
        {
            Anti_Backflow_Reset(BACKFLOW_ACTIVE);
        }
        else if (input.gas_percent > BACKFLOW_NORMAL_THRESHOLD)
        {
            /*
             * 浓度又回到 normal_threshold 以上，说明还没完全恢复。
             * 退回 MONITORING 继续观察，避免恢复阶段误判结束。
             */
            Anti_Backflow_Reset(BACKFLOW_MONITORING);
            active = 0;
            target_rpm = 0;
        }
        else if (Anti_Backflow_ReleaseStable(input.gas_percent))
        {
            Anti_Backflow_Reset(BACKFLOW_IDLE);
            active = 0;
            target_rpm = 0;
        }
        break;

    default:
        Anti_Backflow_Reset(BACKFLOW_IDLE);
        break;
    }

    if (old_state != s_backflow_state)
    {
        LOG_INFO("BACKFLOW %s -> %s gas=%d",
                 Anti_Backflow_StateText(old_state),
                 Anti_Backflow_StateText(s_backflow_state),
                 input.gas_percent);
    }

    if (s_backflow_state == BACKFLOW_ACTIVE)
    {
        active = 1;
        target_rpm = BACKFLOW_ACTIVE_RPM;
    }
    else if (s_backflow_state == BACKFLOW_RECOVERY)
    {
        active = 1;
        target_rpm = BACKFLOW_RECOVERY_RPM;
    }

    Anti_Backflow_UpdateState(active, target_rpm);
}
