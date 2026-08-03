#include "boot_flash.h"
#include "ota_config.h"
#include "stm32f10x_flash.h"
#include <string.h>

int BootFlash_EraseApplication(void)
{
    uint32_t address;
    FLASH_Status status;
    FLASH_Unlock();
    for (address = OTA_APP_BASE_ADDRESS;
         address < OTA_APP_BASE_ADDRESS + OTA_APP_MAX_SIZE;
         address += OTA_FLASH_PAGE_SIZE)
    {
        status = FLASH_ErasePage(address);
        if (status != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
    }
    FLASH_Lock();
    return 1;
}

int BootFlash_Write(uint32_t offset, const uint8_t *data, uint16_t length)
{
    uint32_t address = OTA_APP_BASE_ADDRESS + offset;
    uint16_t i;
    FLASH_Status status;
    if (offset + length > OTA_APP_MAX_SIZE || (offset & 1U) != 0U) { return 0; }
    FLASH_Unlock();
    for (i = 0U; i < length; i += 2U)
    {
        uint16_t value = data[i];
        if (i + 1U < length) { value |= (uint16_t)data[i + 1U] << 8; }
        else { value |= 0xFF00U; }
        status = FLASH_ProgramHalfWord(address + i, value);
        if (status != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
    }
    FLASH_Lock();
    return memcmp((const void *)address, data, length) == 0;
}

int BootFlash_Matches(uint32_t offset, const uint8_t *data, uint16_t length)
{
    if (offset + length > OTA_APP_MAX_SIZE) { return 0; }
    return memcmp((const void *)(OTA_APP_BASE_ADDRESS + offset), data, length) == 0;
}

int BootFlash_VectorIsValid(void)
{
    uint32_t stack = *(const uint32_t *)OTA_APP_BASE_ADDRESS;
    uint32_t reset = *(const uint32_t *)(OTA_APP_BASE_ADDRESS + 4U);
    return stack >= OTA_RAM_BASE_ADDRESS && stack <= OTA_RAM_END_ADDRESS &&
           reset >= OTA_APP_BASE_ADDRESS && reset < OTA_METADATA_A_ADDRESS &&
           (reset & 1U) != 0U;
}
