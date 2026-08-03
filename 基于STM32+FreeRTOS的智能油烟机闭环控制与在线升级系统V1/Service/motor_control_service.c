#include "motor_control_service.h"
#include "pid_service.h"
#include "drv_motor_pwm.h"
#include "message_def.h"
#include "log_service.h"
#include "FreeRTOS.h"
#include "semphr.h"

/* ===================== 手动模式目标转速和开环 PWM ===================== */
#define MOTOR_MANUAL_LOW_RPM             190
#define MOTOR_MANUAL_MID_RPM             205
#define MOTOR_MANUAL_HIGH_RPM            220

#define MOTOR_OPEN_LOOP_LOW_DUTY         450
#define MOTOR_OPEN_LOOP_MID_DUTY         650
#define MOTOR_OPEN_LOOP_HIGH_DUTY        850

/*
 * 小直流风机常见问题：低档 PWM 可以维持转动，但不能可靠从静止启动。
 * 因此从 STOP 切到任意运行档位时，先给一个短时间启动助推，
 * 等风机越过静摩擦和启动死区后，再回落到 LOW/MID/HIGH 的正常占空比。
 */
#define MOTOR_START_BOOST_DUTY           850
#define MOTOR_START_BOOST_MS             500
#define MOTOR_START_BOOST_TICKS          (MOTOR_START_BOOST_MS / MOTOR_CONTROL_SERVICE_PERIOD_MS)

/* ===================== 自动模式目标转速到开环 PWM 的临时映射 ===================== */
#define MOTOR_AUTO_MIN_RPM               160
#define MOTOR_AUTO_MAX_RPM               280
#define MOTOR_AUTO_OPEN_LOOP_MIN_DUTY    380
#define MOTOR_AUTO_OPEN_LOOP_MAX_DUTY    850

#define MOTOR_DUTY_MIN_RUNNING           250
#define MOTOR_PID_DUTY_MAX               850

/* 编码器连续无反馈时退回前馈开环，反馈稳定后再恢复闭环。 */
#define MOTOR_PID_FEEDBACK_LOSS_MS       1000
#define MOTOR_PID_FEEDBACK_RECOVER_MS    250
#define MOTOR_PID_FEEDBACK_LOSS_TICKS    (MOTOR_PID_FEEDBACK_LOSS_MS / MOTOR_CONTROL_SERVICE_PERIOD_MS)
#define MOTOR_PID_FEEDBACK_RECOVER_TICKS (MOTOR_PID_FEEDBACK_RECOVER_MS / MOTOR_CONTROL_SERVICE_PERIOD_MS)

/*
 * 0：开环模式，手动档位/自动目标转速映射为固定 PWM。
 * 1：闭环模式，使用前馈 PWM + 转速 PID 修正；最终 PWM 严格限制为 700。
 */
#define MOTOR_ENCODER_PID_ENABLE         1

/* ===================== PID 参数 ===================== */
#define MOTOR_PID_KP                     30
#define MOTOR_PID_KI                     2
#define MOTOR_PID_KD                     0
#define MOTOR_PID_SCALE                  10
#define MOTOR_PID_INTEGRAL_LIMIT         3000

/* 50ms 控制一次，10 次打印一次，也就是 500ms 打印一次。 */
#define MOTOR_DEBUG_PRINT_PERIOD         10

typedef struct
{
    SystemMode_t mode;
    FanLevel_t fan_level;
    int16_t current_rpm;
    int16_t encoder_delta;
    uint8_t encoder_a_level;
    uint8_t encoder_b_level;
    uint16_t speed_sample_count;
    int16_t auto_target_rpm;
    int16_t backflow_target_rpm;
    uint16_t auto_factor;
    uint8_t backflow_active;
    uint8_t auto_state;
    uint8_t backflow_state;
} MotorControlInput_t;

static PIDController_t s_motor_pid;
static int16_t s_last_target_rpm = -1;
static uint8_t s_motor_running = 0;
static uint8_t s_debug_count = 0;
static uint16_t s_start_boost_ticks = 0;
static SystemMode_t s_last_mode = SYS_MODE_OFF;
static uint16_t s_feedback_loss_ticks = 0;
static uint16_t s_feedback_recover_ticks = 0;
static uint8_t s_pid_open_loop_fallback = 0;

static void Motor_Control_LockState(void)
{
    if (sys_data_mutex != 0)
    {
        (void)xSemaphoreTake(sys_data_mutex, portMAX_DELAY);
    }
}

static void Motor_Control_UnlockState(void)
{
    if (sys_data_mutex != 0)
    {
        xSemaphoreGive(sys_data_mutex);
    }
}

static uint16_t Motor_Control_ClampU16(uint16_t value, uint16_t min, uint16_t max)
{
    if (value < min)
    {
        return min;
    }

    if (value > max)
    {
        return max;
    }

    return value;
}

static uint16_t Motor_Control_ClampDuty(int32_t value, uint16_t min, uint16_t max)
{
    if (value < (int32_t)min)
    {
        return min;
    }

    if (value > (int32_t)max)
    {
        return max;
    }

    return (uint16_t)value;
}

static void Motor_Control_ResetFeedbackMonitor(void)
{
    s_feedback_loss_ticks = 0;
    s_feedback_recover_ticks = 0;
    s_pid_open_loop_fallback = 0;
}

static int16_t Motor_Control_FanLevelToRpm(FanLevel_t level)
{
    /*
     * 手动模式目标转速：
     * STOP 表示电机停止；
     * LOW/MID/HIGH 对应三个固定目标转速。
     */
    switch (level)
    {
    case FAN_LEVEL_STOP:
        return 0;
    case FAN_LEVEL_LOW:
        return MOTOR_MANUAL_LOW_RPM;
    case FAN_LEVEL_MID:
        return MOTOR_MANUAL_MID_RPM;
    case FAN_LEVEL_HIGH:
        return MOTOR_MANUAL_HIGH_RPM;
    default:
        return 0;
    }
}

static uint16_t Motor_Control_FanLevelToDuty(FanLevel_t level)
{
    /*
     * 手动模式暂时使用实测开环占空比。
     * 这些值来自当前电机、12V 供电和风道负载的组合，
     * 后续如果换电机或风道，只需要微调这几个宏。
     */
    switch (level)
    {
    case FAN_LEVEL_LOW:
        return MOTOR_OPEN_LOOP_LOW_DUTY;
    case FAN_LEVEL_MID:
        return MOTOR_OPEN_LOOP_MID_DUTY;
    case FAN_LEVEL_HIGH:
        return MOTOR_OPEN_LOOP_HIGH_DUTY;
    case FAN_LEVEL_STOP:
    default:
        return 0;
    }
}

static uint16_t Motor_Control_TargetToDuty(int16_t target_rpm)
{
    uint32_t duty;

    if (target_rpm <= 0)
    {
        return 0;
    }

    if (target_rpm <= MOTOR_AUTO_MIN_RPM)
    {
        return MOTOR_AUTO_OPEN_LOOP_MIN_DUTY;
    }

    if (target_rpm >= MOTOR_AUTO_MAX_RPM)
    {
        return MOTOR_AUTO_OPEN_LOOP_MAX_DUTY;
    }

    /*
     * 自动模式开环映射：
     * 160RPM -> 380/1000
     * 280RPM -> 850/1000
     *
     * 开环时直接使用该映射；闭环时将它作为前馈值，再叠加 PID 修正量。
     */
    duty = MOTOR_AUTO_OPEN_LOOP_MIN_DUTY;
    duty += ((uint32_t)(target_rpm - MOTOR_AUTO_MIN_RPM) *
             (MOTOR_AUTO_OPEN_LOOP_MAX_DUTY - MOTOR_AUTO_OPEN_LOOP_MIN_DUTY)) /
            (MOTOR_AUTO_MAX_RPM - MOTOR_AUTO_MIN_RPM);

    return Motor_Control_ClampU16((uint16_t)duty,
                                  MOTOR_AUTO_OPEN_LOOP_MIN_DUTY,
                                  MOTOR_AUTO_OPEN_LOOP_MAX_DUTY);
}

static void Motor_Control_ReadInput(MotorControlInput_t *input)
{
    if (input == 0)
    {
        return;
    }

    Motor_Control_LockState();
    input->mode = g_system_state.mode;
    input->fan_level = g_system_state.fan_level;
    input->current_rpm = g_system_state.motor.current_rpm;
    input->encoder_delta = g_system_state.motor.encoder_delta;
    input->encoder_a_level = g_system_state.motor.encoder_a_level;
    input->encoder_b_level = g_system_state.motor.encoder_b_level;
    input->speed_sample_count = g_system_state.motor.speed_sample_count;
    input->auto_target_rpm = g_system_state.motor.auto_target_rpm;
    input->backflow_target_rpm = g_system_state.motor.backflow_target_rpm;
    input->auto_factor = g_system_state.motor.auto_factor;
    input->backflow_active = g_system_state.motor.backflow_active;
    input->auto_state = g_system_state.motor.auto_state;
    input->backflow_state = g_system_state.motor.backflow_state;
    Motor_Control_UnlockState();
}

static void Motor_Control_UpdateState(int16_t target_rpm,
                                      uint16_t duty,
                                      uint8_t enabled)
{
    Motor_Control_LockState();
    g_system_state.motor.target_rpm = target_rpm;
    g_system_state.motor.pwm_duty = duty;
    g_system_state.motor.enabled = enabled;
    g_system_state.motor.direction = MOTOR_DIR_FORWARD;
    Motor_Control_UnlockState();
}

static void Motor_Control_Stop(void)
{
    (void)Motor_PWM_Stop();
    PID_Service_Reset(&s_motor_pid);

    s_motor_running = 0;
    s_debug_count = 0;
    s_start_boost_ticks = 0;
    Motor_Control_ResetFeedbackMonitor();
    Motor_Control_UpdateState(0, 0, 0);
}

static int16_t Motor_Control_SelectTarget(const MotorControlInput_t *input)
{
    int16_t target_rpm = 0;

    if (input == 0)
    {
        return 0;
    }

    if (input->mode == SYS_MODE_MANUAL)
    {
        target_rpm = Motor_Control_FanLevelToRpm(input->fan_level);
    }
    else if (input->mode == SYS_MODE_AUTO)
    {
        /*
         * AUTO 模式下普通自动融合和防回流是两个独立决策源：
         * - auto_target_rpm 表示温湿度/MQ2 综合算法建议的风量；
         * - backflow_target_rpm 表示防回流状态机给出的兜底风量。
         *
         * 最终取更大的目标，保证防回流需要抬速时不会被普通自动调速压低。
         */
        target_rpm = input->auto_target_rpm;

        if (input->backflow_active &&
            input->backflow_target_rpm > target_rpm)
        {
            target_rpm = input->backflow_target_rpm;
        }
    }

    return target_rpm;
}

void Motor_Control_Service_Init(void)
{
    Motor_PWM_Init();

    PID_Service_Init(&s_motor_pid,
                     MOTOR_PID_KP,
                     MOTOR_PID_KI,
                     MOTOR_PID_KD,
                     MOTOR_PID_SCALE,
                     MOTOR_PID_INTEGRAL_LIMIT,
                     -MOTOR_PID_DUTY_MAX,
                     MOTOR_PID_DUTY_MAX);

    Motor_Control_Stop();
}

void Motor_Control_Service_Process(void)
{
    MotorControlInput_t input;
    int16_t target_rpm;
    uint16_t duty = 0;
    uint16_t feedforward_duty = 0;
    int16_t pid_correction = 0;
    uint8_t was_running;
#if MOTOR_ENCODER_PID_ENABLE
    uint8_t feedback_valid;
#endif

    Motor_Control_ReadInput(&input);

    if (input.mode != s_last_mode)
    {
        /*
         * 模式切换时清空 PID 历史量。
         * 这样从 HIGH 切到 LOW 或从 AUTO 切回 MANUAL 时，
         * 不会带着上一模式的积分量继续输出。
         */
        PID_Service_Reset(&s_motor_pid);
        Motor_Control_ResetFeedbackMonitor();
        s_last_target_rpm = -1;
        s_last_mode = input.mode;
    }

    target_rpm = Motor_Control_SelectTarget(&input);

    if (target_rpm <= 0)
    {
        if (s_motor_running || s_last_target_rpm != 0)
        {
            LOG_INFO("MOTOR stop");
        }

        s_last_target_rpm = 0;
        Motor_Control_Stop();
        return;
    }

    was_running = s_motor_running;

    if (!s_motor_running || s_last_target_rpm != target_rpm)
    {
        PID_Service_Reset(&s_motor_pid);
        Motor_Control_ResetFeedbackMonitor();
        (void)Motor_PWM_SetDirection(MOTOR_DIR_FORWARD);
        (void)Motor_PWM_Start();
        s_motor_running = 1;

        if (!was_running || s_last_target_rpm <= 0)
        {
            s_start_boost_ticks = MOTOR_START_BOOST_TICKS;
        }

        LOG_INFO("MOTOR target=%d mode=%d auto=%d backflow=%d",
                 target_rpm,
                 input.mode,
                 input.auto_target_rpm,
                 input.backflow_target_rpm);
    }

#if MOTOR_ENCODER_PID_ENABLE
    if (input.mode == SYS_MODE_MANUAL)
    {
        feedforward_duty = Motor_Control_FanLevelToDuty(input.fan_level);
    }
    else
    {
        feedforward_duty = Motor_Control_TargetToDuty(target_rpm);
    }

    feedforward_duty = Motor_Control_ClampU16(feedforward_duty,
                                               MOTOR_DUTY_MIN_RUNNING,
                                               MOTOR_PID_DUTY_MAX);

    if (s_start_boost_ticks > 0)
    {
        /*
         * 闭环启动阶段同样严格限制在 700，且冻结 PID 历史量。
         * 这样不会在测速尚未建立时累积大误差，也不会由助推逻辑越过闭环上限。
         */
        duty = Motor_Control_ClampU16(MOTOR_START_BOOST_DUTY,
                                      MOTOR_DUTY_MIN_RUNNING,
                                      MOTOR_PID_DUTY_MAX);
        PID_Service_Reset(&s_motor_pid);
        s_feedback_loss_ticks = 0;
        s_feedback_recover_ticks = 0;
        s_start_boost_ticks--;
    }
    else
    {
        feedback_valid = (input.encoder_delta != 0 || input.current_rpm != 0) ? 1U : 0U;

        if (s_pid_open_loop_fallback)
        {
            if (feedback_valid)
            {
                if (s_feedback_recover_ticks < MOTOR_PID_FEEDBACK_RECOVER_TICKS)
                {
                    s_feedback_recover_ticks++;
                }

                if (s_feedback_recover_ticks >= MOTOR_PID_FEEDBACK_RECOVER_TICKS)
                {
                    s_pid_open_loop_fallback = 0;
                    s_feedback_loss_ticks = 0;
                    s_feedback_recover_ticks = 0;
                    PID_Service_Reset(&s_motor_pid);
                    LOG_INFO("MOTOR PID feedback recovered, resume closed loop");
                }
            }
            else
            {
                s_feedback_recover_ticks = 0;
            }
        }
        else if (feedback_valid)
        {
            s_feedback_loss_ticks = 0;
        }
        else
        {
            if (s_feedback_loss_ticks < MOTOR_PID_FEEDBACK_LOSS_TICKS)
            {
                s_feedback_loss_ticks++;
            }

            if (s_feedback_loss_ticks >= MOTOR_PID_FEEDBACK_LOSS_TICKS)
            {
                s_pid_open_loop_fallback = 1;
                s_feedback_recover_ticks = 0;
                PID_Service_Reset(&s_motor_pid);
                LOG_WARN("MOTOR PID feedback lost, fallback open loop pwm=%u",
                         feedforward_duty);
            }
        }

        if (s_pid_open_loop_fallback)
        {
            /* 使用已经实测可测速的前馈占空比维持风机运行。 */
            PID_Service_Reset(&s_motor_pid);
            pid_correction = 0;
            duty = feedforward_duty;
        }
        else
        {
            /*
             * PID 只计算前馈 PWM 上的修正量。
             * 动态输出限幅保证“前馈 + 修正”始终位于 250~700，并配合条件积分抗饱和。
             */
            PID_Service_SetOutputLimits(&s_motor_pid,
                                        (int16_t)((int32_t)MOTOR_DUTY_MIN_RUNNING -
                                                  (int32_t)feedforward_duty),
                                        (int16_t)((int32_t)MOTOR_PID_DUTY_MAX -
                                                  (int32_t)feedforward_duty));
            pid_correction = PID_Service_Calc(&s_motor_pid,
                                              target_rpm,
                                              input.current_rpm);
            duty = Motor_Control_ClampDuty((int32_t)feedforward_duty +
                                           (int32_t)pid_correction,
                                           MOTOR_DUTY_MIN_RUNNING,
                                           MOTOR_PID_DUTY_MAX);
        }
    }
#else
    if (input.mode == SYS_MODE_MANUAL)
    {
        duty = Motor_Control_FanLevelToDuty(input.fan_level);
    }
    else
    {
        duty = Motor_Control_TargetToDuty(target_rpm);
    }

    feedforward_duty = duty;

    if (s_start_boost_ticks > 0)
    {
        if (duty < MOTOR_START_BOOST_DUTY)
        {
            duty = MOTOR_START_BOOST_DUTY;
        }

        s_start_boost_ticks--;
    }
#endif

    (void)Motor_PWM_SetDuty(duty);
    s_last_target_rpm = target_rpm;
    Motor_Control_UpdateState(target_rpm, duty, 1);

    if (++s_debug_count >= MOTOR_DEBUG_PRINT_PERIOD)
    {
        s_debug_count = 0;
        LOG_INFO("MOTOR m=%d rpm=%d d=%d ea=%d eb=%d sp=%u tgt=%d pwm=%d ff=%d corr=%d F=%d as=%d bs=%d bf=%d bst=%d pid=%s fb=%d",
                 input.mode,
                 input.current_rpm,
                 input.encoder_delta,
                 input.encoder_a_level,
                 input.encoder_b_level,
                 input.speed_sample_count,
                 target_rpm,
                 duty,
                 feedforward_duty,
                 pid_correction,
                 input.auto_factor,
                 input.auto_state,
                 input.backflow_state,
                 input.backflow_active,
                 s_start_boost_ticks,
                 MOTOR_ENCODER_PID_ENABLE ? "on" : "off",
                 s_pid_open_loop_fallback);
    }
}
