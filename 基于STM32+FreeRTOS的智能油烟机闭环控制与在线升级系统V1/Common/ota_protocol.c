#include "ota_protocol.h"
#include <string.h>

#define OTA_HEADER_SIZE 12U

static uint32_t Ota_Crc32Update(uint32_t crc, uint8_t byte)
{
    uint8_t bit;

    crc ^= byte;
    for (bit = 0; bit < 8U; bit++)
    {
        crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
    }
    return crc;
}

uint16_t Ota_ReadLe16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

uint32_t Ota_ReadLe32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

void Ota_WriteLe16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

void Ota_WriteLe32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

uint32_t Ota_Protocol_Crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;

    while (length-- != 0U)
    {
        crc = Ota_Crc32Update(crc, *data++);
    }
    return crc ^ 0xFFFFFFFFUL;
}

void Ota_Protocol_ParserInit(OtaProtocolParser_t *parser,
                             OtaFrameCallback_t callback,
                             void *context)
{
    if (parser == 0)
    {
        return;
    }

    memset(parser, 0, sizeof(*parser));
    parser->callback = callback;
    parser->callback_context = context;
}

void Ota_Protocol_ParserFeed(OtaProtocolParser_t *parser, uint8_t byte)
{
    uint16_t payload_length;

    if (parser == 0)
    {
        return;
    }

    if (parser->state == 0U)
    {
        if (byte == OTA_PROTOCOL_SOF_0)
        {
            parser->state = 1U;
        }
        return;
    }

    if (parser->state == 1U)
    {
        if (byte == OTA_PROTOCOL_SOF_1)
        {
            parser->state = 2U;
            parser->index = 0U;
            parser->expected = OTA_HEADER_SIZE;
        }
        else
        {
            parser->state = (byte == OTA_PROTOCOL_SOF_0) ? 1U : 0U;
        }
        return;
    }

    if (parser->index >= sizeof(parser->body))
    {
        parser->state = 0U;
        return;
    }

    parser->body[parser->index++] = byte;
    if (parser->index == OTA_HEADER_SIZE)
    {
        payload_length = Ota_ReadLe16(&parser->body[10]);
        if (parser->body[0] != OTA_PROTOCOL_VERSION ||
            payload_length > OTA_PROTOCOL_MAX_PAYLOAD)
        {
            parser->state = 0U;
            return;
        }
        parser->expected = (uint16_t)(OTA_HEADER_SIZE + payload_length + 4U);
    }

    if (parser->index == parser->expected)
    {
        uint16_t body_length = (uint16_t)(parser->expected - 4U);
        uint32_t received_crc = Ota_ReadLe32(&parser->body[body_length]);
        uint32_t computed_crc = Ota_Protocol_Crc32(parser->body, body_length);

        if (received_crc == computed_crc && parser->callback != 0)
        {
            OtaFrame_t frame;
            frame.type = parser->body[1];
            frame.sequence = Ota_ReadLe32(&parser->body[2]);
            frame.offset = Ota_ReadLe32(&parser->body[6]);
            frame.payload_length = Ota_ReadLe16(&parser->body[10]);
            frame.payload = &parser->body[OTA_HEADER_SIZE];
            parser->callback(&frame, parser->callback_context);
        }
        parser->state = 0U;
    }
}

static void Ota_SendAndCrc(uint8_t byte,
                           uint32_t *crc,
                           OtaSendByteCallback_t send_byte,
                           void *context)
{
    *crc = Ota_Crc32Update(*crc, byte);
    send_byte(byte, context);
}

void Ota_Protocol_Send(uint8_t type,
                       uint32_t sequence,
                       uint32_t offset,
                       const uint8_t *payload,
                       uint16_t payload_length,
                       OtaSendByteCallback_t send_byte,
                       void *context)
{
    uint8_t header[OTA_HEADER_SIZE];
    uint8_t crc_bytes[4];
    uint16_t i;
    uint32_t crc = 0xFFFFFFFFUL;

    if (send_byte == 0 || payload_length > OTA_PROTOCOL_MAX_PAYLOAD ||
        (payload_length != 0U && payload == 0))
    {
        return;
    }

    header[0] = OTA_PROTOCOL_VERSION;
    header[1] = type;
    Ota_WriteLe32(&header[2], sequence);
    Ota_WriteLe32(&header[6], offset);
    Ota_WriteLe16(&header[10], payload_length);

    send_byte(OTA_PROTOCOL_SOF_0, context);
    send_byte(OTA_PROTOCOL_SOF_1, context);
    for (i = 0; i < OTA_HEADER_SIZE; i++)
    {
        Ota_SendAndCrc(header[i], &crc, send_byte, context);
    }
    for (i = 0; i < payload_length; i++)
    {
        Ota_SendAndCrc(payload[i], &crc, send_byte, context);
    }

    crc ^= 0xFFFFFFFFUL;
    Ota_WriteLe32(crc_bytes, crc);
    for (i = 0; i < 4U; i++)
    {
        send_byte(crc_bytes[i], context);
    }
}
