#ifndef __BOOT_P256_H
#define __BOOT_P256_H

#include <stdint.h>

/* Inputs use SEC1 big-endian x||y and ECDSA big-endian r||s formats. */
int BootP256_Verify(const uint8_t public_key[64],
                    const uint8_t hash[32],
                    const uint8_t signature[64]);

#endif /* __BOOT_P256_H */
