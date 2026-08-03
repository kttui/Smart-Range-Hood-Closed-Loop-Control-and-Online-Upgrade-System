#include "boot_metadata.h"
#include "ota_config.h"
#include "ota_protocol.h"
#include "stm32f10x_flash.h"
#include <string.h>

int BootMetadata_RecordIsValid(const BootMetadataRecord_t *record)
{
    uint32_t crc;
    if (record->magic != BOOT_METADATA_MAGIC ||
        record->format != BOOT_METADATA_FORMAT ||
        (record->state != BOOT_METADATA_PENDING && record->state != BOOT_METADATA_VALID) ||
        record->image_size == 0U || record->image_size > OTA_APP_MAX_SIZE)
    {
        return 0;
    }
    crc = Ota_Protocol_Crc32((const uint8_t *)record, 120U);
    return crc == record->record_crc32;
}

int BootMetadata_LoadLatest(BootMetadataRecord_t *record, uint32_t *record_address)
{
    const BootMetadataRecord_t *a = (const BootMetadataRecord_t *)OTA_METADATA_A_ADDRESS;
    const BootMetadataRecord_t *b = (const BootMetadataRecord_t *)OTA_METADATA_B_ADDRESS;
    int valid_a = BootMetadata_RecordIsValid(a);
    int valid_b = BootMetadata_RecordIsValid(b);
    const BootMetadataRecord_t *selected;

    if (!valid_a && !valid_b) { return 0; }
    if (valid_a && valid_b) { selected = (a->generation >= b->generation) ? a : b; }
    else { selected = valid_a ? a : b; }
    memcpy(record, selected, sizeof(*record));
    if (record_address != 0) { *record_address = (uint32_t)selected; }
    return 1;
}

uint32_t BootMetadata_OtherAddress(uint32_t address)
{
    return (address == OTA_METADATA_A_ADDRESS) ? OTA_METADATA_B_ADDRESS : OTA_METADATA_A_ADDRESS;
}

int BootMetadata_Store(const BootMetadataRecord_t *source, uint32_t address)
{
    BootMetadataRecord_t record;
    const uint16_t *halfword;
    uint32_t i;
    FLASH_Status status;

    if (address != OTA_METADATA_A_ADDRESS && address != OTA_METADATA_B_ADDRESS) { return 0; }
    memcpy(&record, source, sizeof(record));
    record.magic = BOOT_METADATA_MAGIC;
    record.format = BOOT_METADATA_FORMAT;
    record.record_crc32 = Ota_Protocol_Crc32((const uint8_t *)&record, 120U);
    record.reserved = 0xFFFFFFFFUL;

    FLASH_Unlock();
    status = FLASH_ErasePage(address);
    if (status != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
    halfword = (const uint16_t *)&record;
    for (i = 0U; i < sizeof(record) / 2U; i++)
    {
        status = FLASH_ProgramHalfWord(address + i * 2U, halfword[i]);
        if (status != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
    }
    FLASH_Lock();
    return memcmp((const void *)address, &record, sizeof(record)) == 0;
}
