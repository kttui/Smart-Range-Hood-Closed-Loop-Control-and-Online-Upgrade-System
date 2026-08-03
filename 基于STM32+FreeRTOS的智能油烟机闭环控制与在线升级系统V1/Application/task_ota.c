#include "task_ota.h"
#include "ota_protocol.h"
#include "ota_boot_control.h"
#include "message_def.h"
#include "bsp_usart.h"
#include "drv_motor_pwm.h"
#include "drv_actuator.h"
#include "log_service.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#define OTA_TASK_NOTIFY_FRAME       (1UL << 0)
#define OTA_TASK_NOTIFY_CHECK       (1UL << 1)
#define OTA_PREPARE_PAYLOAD_SIZE    8U
#define OTA_CHECK_PROGRESS_MS       10000U
#define OTA_CHECK_TIMEOUT_MS        90000U

static TaskHandle_t s_ota_task_handle;
static OtaProtocolParser_t s_parser;
static volatile uint8_t s_prepare_pending;
static volatile uint32_t s_prepare_version;
static volatile uint32_t s_prepare_size;
static volatile uint32_t s_rx_byte_count;
static volatile uint32_t s_rx_frame_count;
static volatile uint8_t s_last_rx_type;
static volatile uint16_t s_last_rx_length;
static uint32_t s_tx_sequence;

static void Ota_Task_SendByte(uint8_t byte, void *context)
{
    (void)context;
    HAL_UART_SendByte(HAL_UART_2, byte);
}

static void Ota_Task_Send(uint8_t type, const uint8_t *payload, uint16_t length)
{
    Ota_Protocol_Send(type,
                      s_tx_sequence++,
                      0U,
                      payload,
                      length,
                      Ota_Task_SendByte,
                      0);
}

static void Ota_Task_OnFrame(const OtaFrame_t *frame, void *context)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)context;
    s_last_rx_type = frame->type;
    s_last_rx_length = frame->payload_length;
    s_rx_frame_count++;

    if (frame->type == OTA_MSG_OTA_PREPARE &&
        frame->payload_length == OTA_PREPARE_PAYLOAD_SIZE)
    {
        s_prepare_version = Ota_ReadLe32(&frame->payload[0]);
        s_prepare_size = Ota_ReadLe32(&frame->payload[4]);
        s_prepare_pending = 1U;
    }

    if (s_ota_task_handle != 0)
    {
        (void)xTaskNotifyFromISR(s_ota_task_handle,
                                 OTA_TASK_NOTIFY_FRAME,
                                 eSetBits,
                                 &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

static void Ota_Task_OnRxByte(uint8_t byte)
{
    s_rx_byte_count++;
    Ota_Protocol_ParserFeed(&s_parser, byte);
}

static uint8_t Ota_Task_IsSafeToInstall(void)
{
    uint8_t safe;

    (void)xSemaphoreTake(sys_data_mutex, portMAX_DELAY);
    safe = (g_system_state.mode == SYS_MODE_OFF &&
            g_system_state.motor.current_rpm == 0 &&
            g_system_state.motor.target_rpm == 0 &&
            g_system_state.motor.enabled == 0 &&
            g_system_state.power_off_request == 0U) ? 1U : 0U;
    xSemaphoreGive(sys_data_mutex);
    return safe;
}

static void Ota_Task_LogBusyReason(void)
{
    uint8_t mode;
    uint16_t current_rpm;
    uint16_t target_rpm;
    uint8_t motor_enabled;
    uint8_t power_off_request;

    (void)xSemaphoreTake(sys_data_mutex, portMAX_DELAY);
    mode = (uint8_t)g_system_state.mode;
    current_rpm = g_system_state.motor.current_rpm;
    target_rpm = g_system_state.motor.target_rpm;
    motor_enabled = g_system_state.motor.enabled;
    power_off_request = g_system_state.power_off_request;
    xSemaphoreGive(sys_data_mutex);

    LOG_WARN("OTA busy: mode=%u rpm=%u target=%u motor=%u poweroff=%u",
             (unsigned int)mode,
             (unsigned int)current_rpm,
             (unsigned int)target_rpm,
             (unsigned int)motor_enabled,
             (unsigned int)power_off_request);
}

static void Ota_Task_EnterBootloader(void)
{
    ActuatorValue_t off;

    off.on = 0U;
    off.rgb_color = RGB_COLOR_OFF;

    (void)xSemaphoreTake(sys_data_mutex, portMAX_DELAY);
    g_system_state.mode = SYS_MODE_UPDATE;
    g_system_state.motor.target_rpm = 0;
    g_system_state.motor.enabled = 0U;
    g_system_state.motor.pwm_duty = 0U;
    g_system_state.output.light_on = 0U;
    g_system_state.output.rgb_state = (uint8_t)RGB_COLOR_RED;
    xSemaphoreGive(sys_data_mutex);

    (void)Motor_PWM_Stop();
    (void)Actuator_Set(&g_led_light_actuator, &off);
    (void)RGB_Led_SetColor(RGB_COLOR_RED);

    Ota_Task_Send(OTA_MSG_OTA_ACCEPT, 0, 0U);
    LOG_INFO("OTA accepted: version=%lu size=%lu",
             (unsigned long)s_prepare_version,
             (unsigned long)s_prepare_size);
    vTaskDelay(pdMS_TO_TICKS(200));

    Ota_BootControl_RequestUpdate();
    taskDISABLE_INTERRUPTS();
    NVIC_SystemReset();
}

void Ota_Task_RequestCheck(void)
{
    if (s_ota_task_handle != 0)
    {
        (void)xTaskNotify(s_ota_task_handle, OTA_TASK_NOTIFY_CHECK, eSetBits);
    }
}

void ota_task(void *pvParameters)
{
    HAL_UartConfig_t uart_config;
    uint32_t notification;
    uint8_t boot_ok_payload[4];
    uint8_t manual_check_waiting = 0U;
    TickType_t manual_check_started = 0U;
    TickType_t last_progress_log = 0U;
    uint32_t manual_check_rx_start = 0U;
    uint32_t manual_check_frame_start = 0U;
    uint32_t reported_frame_count = 0U;

    (void)pvParameters;
    s_ota_task_handle = xTaskGetCurrentTaskHandle();
    Ota_Protocol_ParserInit(&s_parser, Ota_Task_OnFrame, 0);

    memset(&uart_config, 0, sizeof(uart_config));
    uart_config.baudrate = 115200U;
    uart_config.enable_rx_irq = 1U;
    uart_config.preemption_priority = 6U;
    uart_config.sub_priority = 0U;
    HAL_UART_Init(HAL_UART_2, &uart_config);
    HAL_UART_RegisterRxCallback(HAL_UART_2, Ota_Task_OnRxByte);
    LOG_INFO("OTA USART2 ready: TX=PA2 RX=PA3 baud=115200");

    Ota_BootControl_MarkApplicationOk();
    Ota_WriteLe32(boot_ok_payload, OTA_APP_VERSION_CODE);
    Ota_Task_Send(OTA_MSG_APP_BOOT_OK, boot_ok_payload, sizeof(boot_ok_payload));
    LOG_INFO("OTA TX APP_BOOT_OK: version=%lu",
             (unsigned long)OTA_APP_VERSION_CODE);

    while (1)
    {
        TickType_t now;
        uint32_t elapsed_ms;

        notification = 0U;
        (void)xTaskNotifyWait(0U,
                              0xFFFFFFFFUL,
                              &notification,
                              pdMS_TO_TICKS(250));

        if ((notification & OTA_TASK_NOTIFY_CHECK) != 0U)
        {
            /*
             * ESP8266 may boot more slowly than STM32 and miss the one-shot
             * APP_BOOT_OK sent above. Re-send it before every manual check so
             * the ESP knows the running application version and is online.
             */
            Ota_Task_Send(OTA_MSG_APP_BOOT_OK,
                          boot_ok_payload,
                          sizeof(boot_ok_payload));
            LOG_INFO("OTA TX APP_BOOT_OK resync: version=%lu",
                     (unsigned long)OTA_APP_VERSION_CODE);
            Ota_Task_Send(OTA_MSG_OTA_CHECK, 0, 0U);
            manual_check_waiting = 1U;
            manual_check_started = xTaskGetTickCount();
            last_progress_log = manual_check_started;
            manual_check_rx_start = s_rx_byte_count;
            manual_check_frame_start = s_rx_frame_count;
            LOG_INFO("OTA TX CHECK: waiting ESP response (timeout=%us)",
                     (unsigned int)(OTA_CHECK_TIMEOUT_MS / 1000U));
        }

        if (((notification & OTA_TASK_NOTIFY_FRAME) != 0U) &&
            (reported_frame_count != s_rx_frame_count))
        {
            reported_frame_count = s_rx_frame_count;
            LOG_INFO("OTA RX frame: type=0x%02X len=%u frames=%lu bytes=%lu",
                     (unsigned int)s_last_rx_type,
                     (unsigned int)s_last_rx_length,
                     (unsigned long)s_rx_frame_count,
                     (unsigned long)s_rx_byte_count);
        }

        if (s_prepare_pending != 0U)
        {
            manual_check_waiting = 0U;
            LOG_INFO("OTA RX PREPARE: server_version=%lu size=%lu local=%lu",
                     (unsigned long)s_prepare_version,
                     (unsigned long)s_prepare_size,
                     (unsigned long)OTA_APP_VERSION_CODE);

            if (s_prepare_size == 0U || s_prepare_size > OTA_APP_MAX_SIZE ||
                s_prepare_version < OTA_APP_VERSION_CODE)
            {
                uint8_t reason = (uint8_t)OTA_RESULT_BAD_SIZE;
                if (s_prepare_version < OTA_APP_VERSION_CODE)
                {
                    reason = (uint8_t)OTA_RESULT_BAD_VERSION;
                }
                Ota_Task_Send(OTA_MSG_NACK, &reason, 1U);
                LOG_ERROR("OTA rejected: reason=%u version=%lu size=%lu",
                          (unsigned int)reason,
                          (unsigned long)s_prepare_version,
                          (unsigned long)s_prepare_size);
                s_prepare_pending = 0U;
            }
            else if (Ota_Task_IsSafeToInstall() != 0U)
            {
                Ota_Task_EnterBootloader();
            }
            else if ((notification & OTA_TASK_NOTIFY_FRAME) != 0U)
            {
                Ota_Task_Send(OTA_MSG_OTA_BUSY, 0, 0U);
                Ota_Task_LogBusyReason();
            }
        }

        if (manual_check_waiting != 0U)
        {
            now = xTaskGetTickCount();
            elapsed_ms = (uint32_t)((now - manual_check_started) *
                                    portTICK_PERIOD_MS);

            if ((uint32_t)((now - last_progress_log) *
                           portTICK_PERIOD_MS) >= OTA_CHECK_PROGRESS_MS)
            {
                last_progress_log = now;
                LOG_INFO("OTA waiting: %lus rx_bytes=%lu valid_frames=%lu",
                         (unsigned long)(elapsed_ms / 1000U),
                         (unsigned long)(s_rx_byte_count -
                                         manual_check_rx_start),
                         (unsigned long)(s_rx_frame_count -
                                         manual_check_frame_start));
            }

            if (elapsed_ms >= OTA_CHECK_TIMEOUT_MS)
            {
                manual_check_waiting = 0U;
                if (s_rx_byte_count == manual_check_rx_start)
                {
                    LOG_ERROR("OTA timeout: no ESP response (offline/net/no newer)");
                }
                else if (s_rx_frame_count == manual_check_frame_start)
                {
                    LOG_ERROR("OTA timeout: RX bytes exist but no valid frame");
                }
                else
                {
                    LOG_ERROR("OTA timeout: valid frame received, no PREPARE");
                }
            }
        }
    }
}
