#include "system_state.h"
#include "message_def.h"
#include "app_main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "bsp_usart.h"
#include "bsp_delay.h"
#include "misc.h"
#include "drv_console.h"
#include "sensor_task.h"
#include "power_task.h"
#include "task_key.h"
#include "control_task.h"
#include "task_speed.h"
#include "task_auto.h"
#include "task_anti_backflow.h"
#include "task_motor.h"
#include "task_ui.h"
#include "task_log.h"
#include "task_ota.h"
#include "log_service.h"
#include <stdio.h>

/*
 * app_main.c
 *
 * FreeRTOS 应用层启动入口。
 * main.c 只负责上电自锁检查，然后调用 freeRTOS_start()。
 * 从这里开始，系统按“任务 + 服务 + 驱动”的结构运行：
 *
 * 任务层 Application：决定周期和调度方式。
 * 服务层 Service：实现业务逻辑，例如模式切换、自动调速、防回流。
 * 驱动层 Driver/BSP：封装 GPIO、PWM、编码器、LCD、传感器等硬件细节。
 */

SystemState_t g_system_state =
{
    .mode = SYS_MODE_MANUAL,
    .fan_level = FAN_LEVEL_STOP,

    .sensor = {
        .temperature = 0,
        .humidity = 0,
        .gas_percent = 0
    },

    .motor = {
        .current_rpm = 0,
        .encoder_delta = 0,
        .target_rpm = 0,
        .auto_target_rpm = 0,
        .backflow_target_rpm = 0,
        .pwm_duty = 0,
        .auto_factor = 0,
        .enabled = 0,
        .direction = 0,
        .auto_state = 0,
        .backflow_state = 0,
        .backflow_active = 0,
        .encoder_a_level = 0,
        .encoder_b_level = 0,
        .speed_sample_count = 0
    },

    .output = {
        .light_on = 0,
        .buzzer_on = 0,
        .rgb_state = 0
    },

    .alarm_flag = 0,
    .power_off_request = 0
};

/* 系统运行期共享资源。 */
SemaphoreHandle_t uart_mutex;
SemaphoreHandle_t sys_data_mutex;
QueueHandle_t system_event_queue;

/* 启动任务只负责创建资源和业务任务，创建完成后删除自身。 */
#define START_TASK_PRIO             1
#define START_TASK_SIZE             256
TaskHandle_t Start_Task_Handler;
void start_task(void *pvParameters);

/*
 * 当前任务划分：
 *
 * PowerTask        10ms   长按关机/释放自锁电源，优先级最高。
 * KeyTask          10ms   按键扫描、消抖、短按/长按识别。
 * SpeedCalcTask    50ms   编码器测速，更新 current_rpm。
 * MotorControlTask 50ms   根据目标转速输出 PWM，后续可切换 PID。
 * ControlTask      event  消费按键事件，切换模式、档位、照明、升级状态。
 * AutoControlTask  100ms  温度/湿度/MQ2 融合，计算自动模式目标转速。
 * AntiBackFlowTask 100ms  防回流状态机，必要时抬高自动模式目标转速。
 * SensorTask       2s     读取 DHT11 和 MQ2，并打印环境数据。
 * UiTask           200ms  刷新 LCD 状态页，优先级最低。
 */
#define POWER_TASK_PRIO             5
#define KEY_TASK_PRIO               4
#define SPEED_TASK_PRIO             4
#define MOTOR_TASK_PRIO             4
#define CONTROL_TASK_PRIO           3
#define AUTO_TASK_PRIO              3
#define ANTI_BACKFLOW_TASK_PRIO     3
#define SENSOR_TASK_PRIO            2
#define LOG_TASK_PRIO               1
#define UI_TASK_PRIO                1
#define OTA_TASK_PRIO               3

#define POWER_TASK_SIZE             128
#define KEY_TASK_SIZE               192
#define SPEED_TASK_SIZE             128
#define MOTOR_TASK_SIZE             256
#define CONTROL_TASK_SIZE           256
#define AUTO_TASK_SIZE              192
#define ANTI_BACKFLOW_TASK_SIZE     192
#define SENSOR_TASK_SIZE            256
#define LOG_TASK_SIZE               256
#define UI_TASK_SIZE                512
#define OTA_TASK_SIZE               320

TaskHandle_t Power_Task_Handler;
TaskHandle_t Key_Task_Handler;
TaskHandle_t Speed_Task_Handler;
TaskHandle_t Motor_Task_Handler;
TaskHandle_t Control_Task_Handler;
TaskHandle_t Auto_Task_Handler;
TaskHandle_t Anti_Backflow_Task_Handler;
TaskHandle_t Sensor_Task_Handler;
TaskHandle_t Log_Task_Handler;
TaskHandle_t UI_Task_Handler;
TaskHandle_t Ota_Task_Handler;

void freeRTOS_start(void)
{
    BaseType_t ret;

    delay_init();
    Console_Init(115200);

    ret = xTaskCreate(start_task,
                      "start_task",
                      START_TASK_SIZE,
                      NULL,
                      START_TASK_PRIO,
                      &Start_Task_Handler);
    if (ret != pdPASS)
    {
        printf("Failed to create start_task\r\n");
    }

    vTaskStartScheduler();
}

void start_task(void *pvParameters)
{
    (void)pvParameters;

    /*
     * system_event_queue 用于离散事件，例如按键短按/长按。
     * 连续状态量，例如传感器数值、目标转速、防回流状态，
     * 直接写入 g_system_state，避免用队列高频传大结构。
     */
    system_event_queue = xQueueCreate(10, sizeof(SystemEvent_t));
    configASSERT(system_event_queue != NULL);

    /*
     * 日志队列只传递短文本，真正的 printf 由 log_task 统一完成。
     * 这样其他任务写日志时不会长时间占用串口输出。
     */
    log_queue = xQueueCreate(LOG_QUEUE_LENGTH, sizeof(LogMessage_t));
    configASSERT(log_queue != NULL);
    LOG_INFO("FreeRTOS hood system started");

    uart_mutex = xSemaphoreCreateMutex();
    configASSERT(uart_mutex != NULL);

    sys_data_mutex = xSemaphoreCreateMutex();
    configASSERT(sys_data_mutex != NULL);

    xTaskCreate(power_task,
                "power_task",
                POWER_TASK_SIZE,
                NULL,
                POWER_TASK_PRIO,
                &Power_Task_Handler);
    configASSERT(Power_Task_Handler != NULL);

    xTaskCreate(key_task,
                "key_task",
                KEY_TASK_SIZE,
                NULL,
                KEY_TASK_PRIO,
                &Key_Task_Handler);
    configASSERT(Key_Task_Handler != NULL);

    xTaskCreate(speed_task,
                "speed_task",
                SPEED_TASK_SIZE,
                NULL,
                SPEED_TASK_PRIO,
                &Speed_Task_Handler);
    configASSERT(Speed_Task_Handler != NULL);

    xTaskCreate(motor_task,
                "motor_task",
                MOTOR_TASK_SIZE,
                NULL,
                MOTOR_TASK_PRIO,
                &Motor_Task_Handler);
    configASSERT(Motor_Task_Handler != NULL);

    xTaskCreate(control_task,
                "control_task",
                CONTROL_TASK_SIZE,
                NULL,
                CONTROL_TASK_PRIO,
                &Control_Task_Handler);
    configASSERT(Control_Task_Handler != NULL);

    xTaskCreate(auto_control_task,
                "auto_task",
                AUTO_TASK_SIZE,
                NULL,
                AUTO_TASK_PRIO,
                &Auto_Task_Handler);
    configASSERT(Auto_Task_Handler != NULL);

    xTaskCreate(anti_backflow_task,
                "backflow_task",
                ANTI_BACKFLOW_TASK_SIZE,
                NULL,
                ANTI_BACKFLOW_TASK_PRIO,
                &Anti_Backflow_Task_Handler);
    configASSERT(Anti_Backflow_Task_Handler != NULL);

    xTaskCreate(sensor_task,
                "sensor_task",
                SENSOR_TASK_SIZE,
                NULL,
                SENSOR_TASK_PRIO,
                &Sensor_Task_Handler);
    configASSERT(Sensor_Task_Handler != NULL);

    xTaskCreate(log_task,
                "log_task",
                LOG_TASK_SIZE,
                NULL,
                LOG_TASK_PRIO,
                &Log_Task_Handler);
    configASSERT(Log_Task_Handler != NULL);

    xTaskCreate(ota_task,
                "ota_task",
                OTA_TASK_SIZE,
                NULL,
                OTA_TASK_PRIO,
                &Ota_Task_Handler);
    configASSERT(Ota_Task_Handler != NULL);

    xTaskCreate(ui_task,
                "ui_task",
                UI_TASK_SIZE,
                NULL,
                UI_TASK_PRIO,
                &UI_Task_Handler);
    configASSERT(UI_Task_Handler != NULL);

    vTaskDelete(NULL);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;

    taskDISABLE_INTERRUPTS();
    /*
     * FreeRTOS 异常 Hook 触发时系统已经不适合再依赖队列调度，
     * 因此这里保留直接 printf，确保致命错误能尽量输出。
     */
    printf("\r\n[FreeRTOS] Stack overflow! Task: %s\r\n", pcTaskName);

    while (1)
    {
    }
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    /*
     * 内存申请失败属于致命错误路径，此时日志队列可能也无法继续工作，
     * 所以这里同样保留直接 printf 作为兜底提示。
     */
    printf("\r\n[FreeRTOS] Malloc failed!\r\n");

    while (1)
    {
    }
}
