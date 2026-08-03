#ifndef __BOOT_SHA256_H
#define __BOOT_SHA256_H

#include <stdint.h>

typedef struct
{
    uint32_t state[8];
    uint64_t bit_length;
    uint8_t block[64];
    uint32_t block_length;
} BootSha256Context_t;

void BootSha256_Init(BootSha256Context_t *context);
void BootSha256_Update(BootSha256Context_t *context, const uint8_t *data, uint32_t length);
void BootSha256_Final(BootSha256Context_t *context, uint8_t hash[32]);
void BootSha256_Calculate(const uint8_t *data, uint32_t length, uint8_t hash[32]);

#endif /* __BOOT_SHA256_H */
