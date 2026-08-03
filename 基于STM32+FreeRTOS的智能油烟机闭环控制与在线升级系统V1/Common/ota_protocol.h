#ifndef __OTA_PROTOCOL_H
#define __OTA_PROTOCOL_H

#include <stdint.h>
#include "ota_config.h"

typedef enum
{
    OTA_MSG_BL_HELLO = 0x01,
    OTA_MSG_BEGIN = 0x02,
    OTA_MSG_DATA = 0x03,
    OTA_MSG_END = 0x04,
    OTA_MSG_ACK = 0x05,
    OTA_MSG_NACK = 0x06,
    OTA_MSG_RESULT = 0x07,
    OTA_MSG_BL_TRACE = 0x08,

    OTA_MSG_OTA_PREPARE = 0x20,
    OTA_MSG_OTA_ACCEPT = 0x21,
    OTA_MSG_OTA_BUSY = 0x22,
    OTA_MSG_OTA_CHECK = 0x23,
    OTA_MSG_APP_BOOT_OK = 0x24
} OtaMessageType_t;

typedef enum
{
    OTA_RESULT_OK = 0,
    OTA_RESULT_BAD_FRAME = 1,
    OTA_RESULT_BAD_STATE = 2,
    OTA_RESULT_BAD_TARGET = 3,
    OTA_RESULT_BAD_VERSION = 4,
    OTA_RESULT_BAD_SIZE = 5,
    OTA_RESULT_BAD_OFFSET = 6,
    OTA_RESULT_FLASH_ERROR = 7,
    OTA_RESULT_HASH_ERROR = 8,
    OTA_RESULT_SIGNATURE_ERROR = 9,
    OTA_RESULT_TIMEOUT = 10
} OtaResultCode_t;

typedef struct
{
    uint8_t type;
    uint32_t sequence;
    uint32_t offset;
    uint16_t payload_length;
    const uint8_t *payload;
} OtaFrame_t;

typedef void (*OtaFrameCallback_t)(const OtaFrame_t *frame, void *context);
typedef void (*OtaSendByteCallback_t)(uint8_t byte, void *context);

typedef struct
{
    uint8_t state;
    uint16_t index;
    uint16_t expected;
    uint8_t body[12U + OTA_PROTOCOL_MAX_PAYLOAD + 4U];
    OtaFrameCallback_t callback;
    void *callback_context;
} OtaProtocolParser_t;

void Ota_Protocol_ParserInit(OtaProtocolParser_t *parser,
                             OtaFrameCallback_t callback,
                             void *context);
void Ota_Protocol_ParserFeed(OtaProtocolParser_t *parser, uint8_t byte);

uint32_t Ota_Protocol_Crc32(const uint8_t *data, uint32_t length);
void Ota_Protocol_Send(uint8_t type,
                       uint32_t sequence,
                       uint32_t offset,
                       const uint8_t *payload,
                       uint16_t payload_length,
                       OtaSendByteCallback_t send_byte,
                       void *context);

uint16_t Ota_ReadLe16(const uint8_t *data);
uint32_t Ota_ReadLe32(const uint8_t *data);
void Ota_WriteLe16(uint8_t *data, uint16_t value);
void Ota_WriteLe32(uint8_t *data, uint32_t value);

#endif /* __OTA_PROTOCOL_H */
