#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"
#include "stm32f10x_bkp.h"
#include "ota_config.h"
#include "ota_protocol.h"
#include "ota_boot_control.h"
#include "board_pins.h"
#include "boot_crypto.h"
#include "boot_flash.h"
#include "boot_metadata.h"
#include <string.h>

#define BOOTLOADER_VERSION             8UL
#define BOOT_BEGIN_PAYLOAD_SIZE        104U
#define BOOT_NORMAL_WAIT_MS            800U
#define BOOT_HELLO_PERIOD_MS           1000U

typedef struct
{
    uint8_t active;
    uint32_t version_code;
    uint32_t image_size;
    uint32_t expected_offset;
    uint32_t pending_address;
    BootMetadataRecord_t pending_record;
} BootUpdateSession_t;

static volatile uint32_t s_milliseconds;
static OtaProtocolParser_t s_parser;
static BootUpdateSession_t s_session;
static uint32_t s_tx_sequence;
static uint32_t s_current_version;
static uint8_t s_update_complete;
static uint8_t s_app_valid;
static uint8_t s_update_requested;
static uint8_t s_recovery;
static uint8_t s_metadata_state;
static uint32_t s_rx_bytes;
static uint32_t s_rx_dropped;

void SysTick_Handler(void) { s_milliseconds++; }

static void Boot_Delay(uint32_t milliseconds)
{
    uint32_t start = s_milliseconds;
    while ((uint32_t)(s_milliseconds - start) < milliseconds) { }
}

static void Boot_HoldPower(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(PWR_HOLD_GPIO_CLK, ENABLE);
    GPIO_SetBits(PWR_HOLD_PORT, PWR_HOLD_PIN);
    gpio.GPIO_Pin = PWR_HOLD_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PWR_HOLD_PORT, &gpio);
    GPIO_SetBits(PWR_HOLD_PORT, PWR_HOLD_PIN);
}

static void Boot_SetLedChannel(GPIO_TypeDef *port,
                               uint16_t pin,
                               uint8_t active_level,
                               uint8_t on)
{
    if ((on != 0U) == (active_level != 0U))
    {
        GPIO_SetBits(port, pin);
    }
    else
    {
        GPIO_ResetBits(port, pin);
    }
}

static void Boot_StatusLedRed(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(LED_R_GPIO_CLK |
                           LED_G_GPIO_CLK |
                           LED_B_GPIO_CLK,
                           ENABLE);

    Boot_SetLedChannel(LED_R_PORT, LED_R_PIN, LED_R_ACTIVE_LEVEL, 1U);
    Boot_SetLedChannel(LED_G_PORT, LED_G_PIN, LED_G_ACTIVE_LEVEL, 0U);
    Boot_SetLedChannel(LED_B_PORT, LED_B_PIN, LED_B_ACTIVE_LEVEL, 0U);

    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Pin = LED_R_PIN;
    GPIO_Init(LED_R_PORT, &gpio);
    gpio.GPIO_Pin = LED_G_PIN;
    GPIO_Init(LED_G_PORT, &gpio);
    gpio.GPIO_Pin = LED_B_PIN;
    GPIO_Init(LED_B_PORT, &gpio);
}

static void Boot_UsartInit(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    RCC_APB2PeriphClockCmd(USART2_GPIO_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(USART2_CLK, ENABLE);
    gpio.GPIO_Pin = USART2_TX_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(USART2_TX_PORT, &gpio);
    gpio.GPIO_Pin = USART2_RX_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(USART2_RX_PORT, &gpio);
    USART_StructInit(&usart);
    usart.USART_BaudRate = 115200U;
    usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &usart);
    USART_Cmd(USART2, ENABLE);
}

static void Boot_SendByte(uint8_t byte, void *context)
{
    (void)context;
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) { }
    USART_SendData(USART2, byte);
}

static void Boot_SendStatus(uint8_t type, OtaResultCode_t result, uint32_t offset)
{
    uint8_t payload = (uint8_t)result;
    Ota_Protocol_Send(type, s_tx_sequence++, offset, &payload, 1U, Boot_SendByte, 0);
}

static void Boot_SendTrace(uint8_t stage, uint32_t value)
{
    uint8_t payload[5];
    payload[0] = stage;
    Ota_WriteLe32(&payload[1], value);
    Ota_Protocol_Send(OTA_MSG_BL_TRACE,
                      s_tx_sequence++,
                      s_session.expected_offset,
                      payload,
                      sizeof(payload),
                      Boot_SendByte,
                      0);
}

static void Boot_DiscardPendingRx(void)
{
    volatile uint32_t clear_value;
    uint32_t status = USART2->SR;

    if ((status & (USART_FLAG_RXNE | USART_FLAG_ORE |
                   USART_FLAG_NE | USART_FLAG_FE | USART_FLAG_PE)) != 0U)
    {
        clear_value = USART2->DR;
        (void)clear_value;
        s_rx_dropped++;
    }
    s_parser.state = 0U;
    s_parser.index = 0U;
    s_parser.expected = 0U;
}

static void Boot_SendHello(void)
{
    uint8_t payload[28];
    Ota_WriteLe32(&payload[0], BOOTLOADER_VERSION);
    Ota_WriteLe32(&payload[4], s_current_version);
    Ota_WriteLe32(&payload[8], OTA_APP_MAX_SIZE);
    Ota_WriteLe32(&payload[12], s_session.expected_offset);
    payload[16] = s_app_valid;
    payload[17] = s_update_requested;
    payload[18] = s_recovery;
    payload[19] = s_metadata_state;
    Ota_WriteLe32(&payload[20], s_rx_bytes);
    Ota_WriteLe32(&payload[24], s_rx_dropped);
    Ota_Protocol_Send(OTA_MSG_BL_HELLO,
                      s_tx_sequence++,
                      s_session.expected_offset,
                      payload,
                      sizeof(payload),
                      Boot_SendByte,
                      0);
}

static uint8_t Boot_ApplicationIsValid(const BootMetadataRecord_t *record)
{
    uint8_t hash[32];
    if (record == 0 || record->state != BOOT_METADATA_VALID || !BootFlash_VectorIsValid())
    {
        return 0U;
    }
    BootCrypto_HashFlash(OTA_APP_BASE_ADDRESS, record->image_size, hash);
    return memcmp(hash, record->image_hash, sizeof(hash)) == 0 ? 1U : 0U;
}

static void Boot_HandleBegin(const OtaFrame_t *frame)
{
    BootMetadataRecord_t latest;
    uint32_t latest_address = OTA_METADATA_B_ADDRESS;
    uint32_t version;
    uint32_t size;
    uint32_t generation = 1U;
    uint8_t current_app_valid = 0U;

    Boot_StatusLedRed();
    Boot_SendTrace(0x10U, frame->payload_length);
    if (frame->payload_length != BOOT_BEGIN_PAYLOAD_SIZE)
    {
        Boot_SendStatus(OTA_MSG_NACK, OTA_RESULT_BAD_FRAME, 0U); return;
    }
    version = Ota_ReadLe32(&frame->payload[0]);
    size = Ota_ReadLe32(&frame->payload[4]);
    if (size == 0U || size > OTA_APP_MAX_SIZE)
    {
        Boot_SendStatus(OTA_MSG_NACK, OTA_RESULT_BAD_SIZE, 0U); return;
    }
    if (BootMetadata_LoadLatest(&latest, &latest_address))
    {
        generation = latest.generation + 1U;
        current_app_valid = Boot_ApplicationIsValid(&latest);
        if (version < latest.version_code ||
            (version == latest.version_code && current_app_valid != 0U))
        {
            Boot_SendStatus(OTA_MSG_NACK, OTA_RESULT_BAD_VERSION, 0U); return;
        }
    }
    if (!BootCrypto_VerifyManifest(version,
                                   size,
                                   &frame->payload[8],
                                   &frame->payload[40]))
    {
        Boot_SendStatus(OTA_MSG_NACK, OTA_RESULT_SIGNATURE_ERROR, 0U); return;
    }
    Boot_SendTrace(0x11U, version);

    memset(&s_session, 0, sizeof(s_session));
    s_session.version_code = version;
    s_session.image_size = size;
    s_session.pending_address = BootMetadata_OtherAddress(latest_address);
    memset(&s_session.pending_record, 0xFF, sizeof(s_session.pending_record));
    s_session.pending_record.generation = generation;
    s_session.pending_record.state = BOOT_METADATA_PENDING;
    s_session.pending_record.version_code = version;
    s_session.pending_record.image_size = size;
    memcpy(s_session.pending_record.image_hash, &frame->payload[8], 32U);
    memcpy(s_session.pending_record.signature, &frame->payload[40], 64U);

    if (!BootMetadata_Store(&s_session.pending_record, s_session.pending_address))
    {
        Boot_SendStatus(OTA_MSG_NACK, OTA_RESULT_FLASH_ERROR, 0U); return;
    }
    Boot_SendTrace(0x12U, s_session.pending_address);
    if (!BootFlash_EraseApplication())
    {
        Boot_SendStatus(OTA_MSG_NACK, OTA_RESULT_FLASH_ERROR, 0U); return;
    }
    Boot_SendTrace(0x13U, FLASH->SR);
    /*
     * Signature verification and a full application erase are intentionally
     * blocking.  Discard a stale byte/error left by an older gateway retry
     * before acknowledging that the bootloader is ready for DATA.
     */
    Boot_DiscardPendingRx();
    s_session.active = 1U;
    s_session.expected_offset = 0U;
    s_current_version = 0U;
    s_app_valid = 0U;
    s_metadata_state = 1U;
    Boot_SendStatus(OTA_MSG_ACK, OTA_RESULT_OK, 0U);
}

static void Boot_HandleData(const OtaFrame_t *frame)
{
    Boot_SendTrace(1U, frame->payload_length);
    if (s_session.active == 0U || frame->payload_length == 0U ||
        frame->offset + frame->payload_length > s_session.image_size)
    {
        Boot_SendTrace(0xE1U, frame->offset);
        Boot_SendStatus(OTA_MSG_NACK, OTA_RESULT_BAD_STATE, s_session.expected_offset); return;
    }
    if (frame->offset < s_session.expected_offset &&
        frame->offset + frame->payload_length <= s_session.expected_offset &&
        BootFlash_Matches(frame->offset, frame->payload, frame->payload_length))
    {
        Boot_SendTrace(4U, s_session.expected_offset);
        Boot_SendStatus(OTA_MSG_ACK, OTA_RESULT_OK, s_session.expected_offset); return;
    }
    if (frame->offset != s_session.expected_offset ||
        ((frame->payload_length & 1U) != 0U &&
         frame->offset + frame->payload_length != s_session.image_size))
    {
        Boot_SendTrace(0xE2U, frame->offset);
        Boot_SendStatus(OTA_MSG_NACK, OTA_RESULT_BAD_OFFSET, s_session.expected_offset); return;
    }
    Boot_SendTrace(2U, FLASH->SR);
    if (!BootFlash_Write(frame->offset, frame->payload, frame->payload_length))
    {
        Boot_SendTrace(0xE3U, FLASH->SR);
        Boot_SendStatus(OTA_MSG_NACK, OTA_RESULT_FLASH_ERROR, s_session.expected_offset); return;
    }
    s_session.expected_offset += frame->payload_length;
    Boot_SendTrace(3U, s_session.expected_offset);
    Boot_SendStatus(OTA_MSG_ACK, OTA_RESULT_OK, s_session.expected_offset);
}

static void Boot_HandleEnd(const OtaFrame_t *frame)
{
    uint8_t hash[32];
    BootMetadataRecord_t valid;
    uint32_t valid_address;
    (void)frame;

    if (s_session.active == 0U || s_session.expected_offset != s_session.image_size)
    {
        Boot_SendStatus(OTA_MSG_NACK, OTA_RESULT_BAD_STATE, s_session.expected_offset); return;
    }
    BootCrypto_HashFlash(OTA_APP_BASE_ADDRESS, s_session.image_size, hash);
    if (memcmp(hash, s_session.pending_record.image_hash, sizeof(hash)) != 0)
    {
        Boot_SendStatus(OTA_MSG_RESULT, OTA_RESULT_HASH_ERROR, s_session.expected_offset); return;
    }
    if (!BootFlash_VectorIsValid())
    {
        Boot_SendStatus(OTA_MSG_RESULT, OTA_RESULT_BAD_FRAME, s_session.expected_offset); return;
    }

    memcpy(&valid, &s_session.pending_record, sizeof(valid));
    valid.state = BOOT_METADATA_VALID;
    valid.generation++;
    valid_address = BootMetadata_OtherAddress(s_session.pending_address);
    if (!BootMetadata_Store(&valid, valid_address))
    {
        Boot_SendStatus(OTA_MSG_RESULT, OTA_RESULT_FLASH_ERROR, s_session.expected_offset); return;
    }

    s_current_version = valid.version_code;
    s_app_valid = 1U;
    s_update_requested = 0U;
    s_recovery = 0U;
    s_metadata_state = 2U;
    s_session.active = 0U;
    Ota_BootControl_ClearUpdateRequest();
    Boot_SendStatus(OTA_MSG_RESULT, OTA_RESULT_OK, s_session.expected_offset);
    s_update_complete = 1U;
}

static void Boot_OnFrame(const OtaFrame_t *frame, void *context)
{
    (void)context;
    switch (frame->type)
    {
    case OTA_MSG_BEGIN: Boot_HandleBegin(frame); break;
    case OTA_MSG_DATA: Boot_HandleData(frame); break;
    case OTA_MSG_END: Boot_HandleEnd(frame); break;
    default: Boot_SendStatus(OTA_MSG_NACK, OTA_RESULT_BAD_FRAME, s_session.expected_offset); break;
    }
}

static void Boot_JumpToApplication(void)
{
    typedef void (*EntryPoint_t)(void);
    uint32_t stack = *(const uint32_t *)OTA_APP_BASE_ADDRESS;
    uint32_t reset = *(const uint32_t *)(OTA_APP_BASE_ADDRESS + 4U);
    EntryPoint_t entry = (EntryPoint_t)reset;
    uint32_t i;

    Ota_BootControl_MarkHandoff();
    BKP_WriteBackupRegister(BKP_DR3, 0U);
    BKP_WriteBackupRegister(BKP_DR5, OTA_BKP_TRY_MAGIC);
    __disable_irq();
    SysTick->CTRL = 0U; SysTick->LOAD = 0U; SysTick->VAL = 0U;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    for (i = 0U; i < 8U; i++) { NVIC->ICER[i] = 0xFFFFFFFFUL; NVIC->ICPR[i] = 0xFFFFFFFFUL; }
    USART_Cmd(USART2, DISABLE);
    RCC_DeInit();
    SCB->VTOR = OTA_APP_BASE_ADDRESS;
    __set_BASEPRI(0U);
    __set_FAULTMASK(0U);
    __set_CONTROL(0U);
    __set_MSP(stack);
    __DSB();
    __ISB();
    __enable_irq();
    entry();
    while (1) { }
}

static uint8_t Boot_UpdateCrashCounter(void)
{
    uint16_t failures;
    Ota_BootControl_Init();
    if (BKP_ReadBackupRegister(BKP_DR3) == OTA_BKP_BOOT_OK_MAGIC)
    {
        BKP_WriteBackupRegister(BKP_DR3, 0U);
        BKP_WriteBackupRegister(BKP_DR4, 0U);
        BKP_WriteBackupRegister(BKP_DR5, 0U);
        return 0U;
    }
    failures = BKP_ReadBackupRegister(BKP_DR4);
    if (BKP_ReadBackupRegister(BKP_DR5) == OTA_BKP_TRY_MAGIC)
    {
        BKP_WriteBackupRegister(BKP_DR5, 0U);
        if (failures < 0xFFFFU) { failures++; }
        BKP_WriteBackupRegister(BKP_DR4, failures);
    }
    return failures >= OTA_MAX_FAILED_BOOTS ? 1U : 0U;
}

int main(void)
{
    BootMetadataRecord_t metadata;
    uint32_t metadata_address;
    uint32_t last_hello;
    uint32_t start;
    uint8_t app_valid = 0U;
    uint8_t recovery;
    uint8_t update_requested;

    Boot_HoldPower();
    Ota_BootControl_Init();
    recovery = Boot_UpdateCrashCounter();
    update_requested = Ota_BootControl_UpdateRequested();
    s_recovery = recovery;
    s_update_requested = update_requested;
    Boot_UsartInit();
    Ota_Protocol_ParserInit(&s_parser, Boot_OnFrame, 0);
    (void)SysTick_Config(SystemCoreClock / 1000U);

    if (BootMetadata_LoadLatest(&metadata, &metadata_address))
    {
        s_metadata_state = (metadata.state == BOOT_METADATA_VALID) ? 2U : 1U;
        app_valid = Boot_ApplicationIsValid(&metadata);
        /*
         * A PENDING record may already contain the requested version even
         * though the application transfer was interrupted or its hash is
         * invalid.  Advertising that version would make the ESP believe the
         * cached image is not newer, so it would never retry the transfer.
         */
        s_current_version = (app_valid != 0U) ? metadata.version_code : 0U;
    }
    s_app_valid = app_valid;
    if (update_requested != 0U || recovery != 0U || app_valid == 0U)
    {
        Boot_StatusLedRed();
    }
    Boot_SendHello();
    start = s_milliseconds;
    last_hello = start;

    while (1)
    {
        if (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET)
        {
            Ota_Protocol_ParserFeed(&s_parser, (uint8_t)USART_ReceiveData(USART2));
            s_rx_bytes++;
        }
        if (s_update_complete != 0U)
        {
            Boot_Delay(200U);
            Boot_JumpToApplication();
        }
        if (s_session.active == 0U &&
            (uint32_t)(s_milliseconds - last_hello) >= BOOT_HELLO_PERIOD_MS)
        {
            Boot_SendHello();
            last_hello = s_milliseconds;
        }
        if (app_valid != 0U && update_requested == 0U && recovery == 0U &&
            s_session.active == 0U &&
            (uint32_t)(s_milliseconds - start) >= BOOT_NORMAL_WAIT_MS)
        {
            Boot_JumpToApplication();
        }
    }
}
