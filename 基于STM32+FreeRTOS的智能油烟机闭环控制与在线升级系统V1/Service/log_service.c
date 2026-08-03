#include "log_service.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

QueueHandle_t log_queue = NULL;

BaseType_t Log_Service_Write(LogLevel_t level, const char *fmt, ...)
{
    LogMessage_t msg;
    va_list args;

    if ((log_queue == NULL) || (fmt == NULL))
    {
        return errQUEUE_FULL;
    }

    msg.level = level;
    memset(msg.text, 0, sizeof(msg.text));

    va_start(args, fmt);
    vsnprintf(msg.text, sizeof(msg.text), fmt, args);
    va_end(args);

    return xQueueSend(log_queue, &msg, pdMS_TO_TICKS(10));
}
