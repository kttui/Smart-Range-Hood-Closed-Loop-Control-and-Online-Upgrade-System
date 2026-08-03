#include "boot_crypto.h"
#include "boot_sha256.h"
#include "boot_p256.h"
#include "ota_config.h"
#include "ota_public_key.h"
#include <string.h>

static uint32_t BootCrypto_AppendText(uint8_t *buffer, uint32_t offset, const char *text)
{
    while (*text != '\0') { buffer[offset++] = (uint8_t)*text++; }
    return offset;
}

static uint32_t BootCrypto_AppendDecimal(uint8_t *buffer, uint32_t offset, uint32_t value)
{
    uint8_t digits[10];
    uint32_t count = 0U;
    do { digits[count++] = (uint8_t)('0' + value % 10U); value /= 10U; } while (value != 0U);
    while (count != 0U) { buffer[offset++] = digits[--count]; }
    return offset;
}

int BootCrypto_VerifyManifest(uint32_t version_code,
                              uint32_t image_size,
                              const uint8_t image_hash[32],
                              const uint8_t signature[64])
{
    static const char hex[] = "0123456789abcdef";
    uint8_t canonical[160];
    uint8_t digest[32];
    uint32_t i;
    uint32_t length = 0U;

    length = BootCrypto_AppendText(canonical, length, "hood-ota-v1\n");
    length = BootCrypto_AppendText(canonical, length, OTA_TARGET_NAME);
    canonical[length++] = '\n';
    length = BootCrypto_AppendDecimal(canonical, length, version_code);
    canonical[length++] = '\n';
    length = BootCrypto_AppendDecimal(canonical, length, image_size);
    canonical[length++] = '\n';
    for (i = 0U; i < 32U; i++)
    {
        canonical[length++] = (uint8_t)hex[image_hash[i] >> 4];
        canonical[length++] = (uint8_t)hex[image_hash[i] & 0x0FU];
    }
    canonical[length++] = '\n';
    BootSha256_Calculate(canonical, length, digest);
    return BootP256_Verify(g_ota_signing_public_key, digest, signature);
}

void BootCrypto_HashFlash(uint32_t address, uint32_t length, uint8_t hash[32])
{
    BootSha256Context_t context;
    const uint8_t *flash = (const uint8_t *)address;
    BootSha256_Init(&context);
    while (length != 0U)
    {
        uint32_t chunk = (length > 1024U) ? 1024U : length;
        BootSha256_Update(&context, flash, chunk);
        flash += chunk;
        length -= chunk;
    }
    BootSha256_Final(&context, hash);
}
