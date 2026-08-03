#ifndef __LOG_SERVICE_H
#define __LOG_SERVICE_H

#include "FreeRTOS.h"
#include "queue.h"
#include <stdint.h>

/* Log queue depth and one-message text buffer size. */
#define LOG_QUEUE_LENGTH      16
#define LOG_TEXT_MAX_LEN      128

typedef enum
{
    LOG_LEVEL_INFO = 0,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel_t;

typedef struct
{
    LogLevel_t level;
    char text[LOG_TEXT_MAX_LEN];
} LogMessage_t;

/* Global log queue, created during system startup. */
extern QueueHandle_t log_queue;

/* Write one formatted log message from task context. */
BaseType_t Log_Service_Write(LogLevel_t level, const char *fmt, ...);

/* Common logging helpers. */
#define LOG_INFO(fmt, ...)   Log_Service_Write(LOG_LEVEL_INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)   Log_Service_Write(LOG_LEVEL_WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)  Log_Service_Write(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)

#endif
