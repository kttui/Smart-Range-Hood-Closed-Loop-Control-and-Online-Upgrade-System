#ifndef __BOOT_CRYPTO_H
#define __BOOT_CRYPTO_H

#include <stdint.h>

int BootCrypto_VerifyManifest(uint32_t version_code,
                              uint32_t image_size,
                              const uint8_t image_hash[32],
                              const uint8_t signature[64]);
void BootCrypto_HashFlash(uint32_t address, uint32_t length, uint8_t hash[32]);

#endif /* __BOOT_CRYPTO_H */
