#ifndef __BOOT_METADATA_H
#define __BOOT_METADATA_H

#include <stdint.h>

#define BOOT_METADATA_MAGIC        0x4F54414DUL
#define BOOT_METADATA_FORMAT       1UL
#define BOOT_METADATA_PENDING      0x50454E44UL
#define BOOT_METADATA_VALID        0x56414C44UL

typedef struct
{
    uint32_t magic;
    uint32_t format;
    uint32_t generation;
    uint32_t state;
    uint32_t version_code;
    uint32_t image_size;
    uint8_t image_hash[32];
    uint8_t signature[64];
    uint32_t record_crc32;
    uint32_t reserved;
} BootMetadataRecord_t;

int BootMetadata_LoadLatest(BootMetadataRecord_t *record, uint32_t *record_address);
int BootMetadata_Store(const BootMetadataRecord_t *record, uint32_t address);
uint32_t BootMetadata_OtherAddress(uint32_t address);
int BootMetadata_RecordIsValid(const BootMetadataRecord_t *record);

#endif /* __BOOT_METADATA_H */
