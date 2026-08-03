#include "pid_service.h"

static int32_t PID_Service_Clamp32(int32_t value, int32_t min, int32_t max)
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

void PID_Service_Init(PIDController_t *pid,
                      int16_t kp,
                      int16_t ki,
                      int16_t kd,
                      int16_t scale,
                      int32_t integral_limit,
                      int16_t output_min,
                      int16_t output_max)
{
    if (pid == 0)
    {
        return;
    }

    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->scale = (scale == 0) ? 1 : scale;
    pid->integral_min = -integral_limit;
    pid->integral_max = integral_limit;
    PID_Service_SetOutputLimits(pid, output_min, output_max);

    PID_Service_Reset(pid);
}

void PID_Service_Reset(PIDController_t *pid)
{
    if (pid == 0)
    {
        return;
    }

    pid->integral = 0;
    pid->last_error = 0;
}

void PID_Service_SetOutputLimits(PIDController_t *pid,
                                 int16_t output_min,
                                 int16_t output_max)
{
    int16_t temp;

    if (pid == 0)
    {
        return;
    }

    if (output_min > output_max)
    {
        temp = output_min;
        output_min = output_max;
        output_max = temp;
    }

    pid->output_min = output_min;
    pid->output_max = output_max;
}

int16_t PID_Service_Calc(PIDController_t *pid, int16_t target, int16_t actual)
{
    int16_t error;
    int16_t derivative;
    int32_t candidate_integral;
    int32_t candidate_output;
    int32_t output;

    if (pid == 0)
    {
        return 0;
    }

    /*
     * 整数位置式 PID；输出可作为执行量，也可作为前馈值上的修正量。
     * 使用整数比例系数，避免 STM32F103 没有硬件 FPU 时引入过多浮点开销。
     */
    error = target - actual;
    derivative = error - pid->last_error;
    pid->last_error = error;

    /*
     * 条件积分抗饱和：
     * 如果本次积分会让输出沿着误差方向继续超过上下限，则暂不接收本次积分；
     * 当误差方向有助于退出饱和区时，仍允许积分回退。
     */
    candidate_integral = pid->integral + error;
    candidate_integral = PID_Service_Clamp32(candidate_integral,
                                             pid->integral_min,
                                             pid->integral_max);

    candidate_output = ((int32_t)pid->kp * error) +
                       ((int32_t)pid->ki * candidate_integral) +
                       ((int32_t)pid->kd * derivative);
    candidate_output /= pid->scale;

    if (!((candidate_output > pid->output_max && error > 0) ||
          (candidate_output < pid->output_min && error < 0)))
    {
        pid->integral = candidate_integral;
    }

    output = ((int32_t)pid->kp * error) +
             ((int32_t)pid->ki * pid->integral) +
             ((int32_t)pid->kd * derivative);
    output /= pid->scale;

    output = PID_Service_Clamp32(output, pid->output_min, pid->output_max);

    return (int16_t)output;
}
