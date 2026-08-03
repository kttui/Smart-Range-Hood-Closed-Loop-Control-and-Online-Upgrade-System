#ifndef __OTA_CONFIG_H
#define __OTA_CONFIG_H

#include <stdint.h>

/* STM32F103RCT6: 256 KiB flash, 2 KiB erase pages. */
#define OTA_FLASH_BASE_ADDRESS        0x08000000UL
#define OTA_BOOTLOADER_SIZE           0x00008000UL
#define OTA_APP_BASE_ADDRESS          0x08008000UL
#define OTA_APP_MAX_SIZE              0x00037000UL
#define OTA_METADATA_A_ADDRESS        0x0803F000UL
#define OTA_METADATA_B_ADDRESS        0x0803F800UL
#define OTA_FLASH_PAGE_SIZE           0x00000800UL
#define OTA_RAM_BASE_ADDRESS          0x20000000UL
#define OTA_RAM_END_ADDRESS           0x2000C000UL

#define OTA_TARGET_NAME               "hood-stm32f103rc-v1"
#define OTA_TARGET_NAME_LENGTH        20U

/* Increment this for every application release. */
#ifndef OTA_APP_VERSION_CODE
#define OTA_APP_VERSION_CODE          10000L
//#define OTA_APP_VERSION_CODE          10000UL
#endif

#define OTA_PROTOCOL_VERSION          1U
#define OTA_PROTOCOL_MAX_PAYLOAD      1024U
#define OTA_PROTOCOL_SOF_0            0xA5U
#define OTA_PROTOCOL_SOF_1            0x5AU

/* Backup-register values shared by the application and bootloader. */
#define OTA_BKP_REQUEST_MAGIC          0x4F54U
#define OTA_BKP_HANDOFF_MAGIC          0x484FU
#define OTA_BKP_BOOT_OK_MAGIC          0xB007U
#define OTA_BKP_TRY_MAGIC              0x5452U
#define OTA_MAX_FAILED_BOOTS           3U

#endif /* __OTA_CONFIG_H */
