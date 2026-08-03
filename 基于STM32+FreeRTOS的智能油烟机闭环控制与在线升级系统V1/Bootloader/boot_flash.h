#ifndef __BOOT_FLASH_H
#define __BOOT_FLASH_H

#include <stdint.h>

int BootFlash_EraseApplication(void);
int BootFlash_Write(uint32_t offset, const uint8_t *data, uint16_t length);
int BootFlash_Matches(uint32_t offset, const uint8_t *data, uint16_t length);
int BootFlash_VectorIsValid(void);

#endif /* __BOOT_FLASH_H */
