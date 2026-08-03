#ifndef __PID_SERVICE_H
#define __PID_SERVICE_H

#include <stdint.h>

typedef struct
{
    int16_t kp;
    int16_t ki;
    int16_t kd;
    int16_t scale;

    int32_t integral;
    int32_t integral_min;
    int32_t integral_max;

    int16_t last_error;
    int16_t output_min;
    int16_t output_max;
} PIDController_t;

void PID_Service_Init(PIDController_t *pid,
                      int16_t kp,
                      int16_t ki,
                      int16_t kd,
                      int16_t scale,
                      int32_t integral_limit,
                      int16_t output_min,
                      int16_t output_max);
void PID_Service_Reset(PIDController_t *pid);
void PID_Service_SetOutputLimits(PIDController_t *pid,
                                 int16_t output_min,
                                 int16_t output_max);
int16_t PID_Service_Calc(PIDController_t *pid, int16_t target, int16_t actual);

#endif
