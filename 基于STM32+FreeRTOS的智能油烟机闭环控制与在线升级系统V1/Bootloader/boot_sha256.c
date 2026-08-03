#include "boot_sha256.h"

static const uint32_t s_sha256_k[64] = {
    0x428A2F98UL, 0x71374491UL, 0xB5C0FBCFUL, 0xE9B5DBA5UL,
    0x3956C25BUL, 0x59F111F1UL, 0x923F82A4UL, 0xAB1C5ED5UL,
    0xD807AA98UL, 0x12835B01UL, 0x243185BEUL, 0x550C7DC3UL,
    0x72BE5D74UL, 0x80DEB1FEUL, 0x9BDC06A7UL, 0xC19BF174UL,
    0xE49B69C1UL, 0xEFBE4786UL, 0x0FC19DC6UL, 0x240CA1CCUL,
    0x2DE92C6FUL, 0x4A7484AAUL, 0x5CB0A9DCUL, 0x76F988DAUL,
    0x983E5152UL, 0xA831C66DUL, 0xB00327C8UL, 0xBF597FC7UL,
    0xC6E00BF3UL, 0xD5A79147UL, 0x06CA6351UL, 0x14292967UL,
    0x27B70A85UL, 0x2E1B2138UL, 0x4D2C6DFCUL, 0x53380D13UL,
    0x650A7354UL, 0x766A0ABBUL, 0x81C2C92EUL, 0x92722C85UL,
    0xA2BFE8A1UL, 0xA81A664BUL, 0xC24B8B70UL, 0xC76C51A3UL,
    0xD192E819UL, 0xD6990624UL, 0xF40E3585UL, 0x106AA070UL,
    0x19A4C116UL, 0x1E376C08UL, 0x2748774CUL, 0x34B0BCB5UL,
    0x391C0CB3UL, 0x4ED8AA4AUL, 0x5B9CCA4FUL, 0x682E6FF3UL,
    0x748F82EEUL, 0x78A5636FUL, 0x84C87814UL, 0x8CC70208UL,
    0x90BEFFFAUL, 0xA4506CEBUL, 0xBEF9A3F7UL, 0xC67178F2UL
};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32U - (n))))

static uint32_t BootSha256_ReadBe32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static void BootSha256_Transform(BootSha256Context_t *context)
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t s0, s1, choice, majority, temp1, temp2;
    uint32_t i;

    for (i = 0; i < 16U; i++)
    {
        w[i] = BootSha256_ReadBe32(&context->block[i * 4U]);
    }
    for (i = 16U; i < 64U; i++)
    {
        s0 = ROR32(w[i - 15U], 7U) ^ ROR32(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
        s1 = ROR32(w[i - 2U], 17U) ^ ROR32(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
        w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }

    a = context->state[0]; b = context->state[1];
    c = context->state[2]; d = context->state[3];
    e = context->state[4]; f = context->state[5];
    g = context->state[6]; h = context->state[7];

    for (i = 0; i < 64U; i++)
    {
        s1 = ROR32(e, 6U) ^ ROR32(e, 11U) ^ ROR32(e, 25U);
        choice = (e & f) ^ ((~e) & g);
        temp1 = h + s1 + choice + s_sha256_k[i] + w[i];
        s0 = ROR32(a, 2U) ^ ROR32(a, 13U) ^ ROR32(a, 22U);
        majority = (a & b) ^ (a & c) ^ (b & c);
        temp2 = s0 + majority;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    context->state[0] += a; context->state[1] += b;
    context->state[2] += c; context->state[3] += d;
    context->state[4] += e; context->state[5] += f;
    context->state[6] += g; context->state[7] += h;
}

void BootSha256_Init(BootSha256Context_t *context)
{
    context->state[0] = 0x6A09E667UL; context->state[1] = 0xBB67AE85UL;
    context->state[2] = 0x3C6EF372UL; context->state[3] = 0xA54FF53AUL;
    context->state[4] = 0x510E527FUL; context->state[5] = 0x9B05688CUL;
    context->state[6] = 0x1F83D9ABUL; context->state[7] = 0x5BE0CD19UL;
    context->bit_length = 0U;
    context->block_length = 0U;
}

void BootSha256_Update(BootSha256Context_t *context, const uint8_t *data, uint32_t length)
{
    while (length-- != 0U)
    {
        context->block[context->block_length++] = *data++;
        if (context->block_length == 64U)
        {
            BootSha256_Transform(context);
            context->bit_length += 512U;
            context->block_length = 0U;
        }
    }
}

void BootSha256_Final(BootSha256Context_t *context, uint8_t hash[32])
{
    uint32_t i;
    uint64_t total_bits = context->bit_length + ((uint64_t)context->block_length * 8U);

    context->block[context->block_length++] = 0x80U;
    if (context->block_length > 56U)
    {
        while (context->block_length < 64U)
        {
            context->block[context->block_length++] = 0U;
        }
        BootSha256_Transform(context);
        context->block_length = 0U;
    }
    while (context->block_length < 56U)
    {
        context->block[context->block_length++] = 0U;
    }
    for (i = 0; i < 8U; i++)
    {
        context->block[63U - i] = (uint8_t)(total_bits >> (i * 8U));
    }
    BootSha256_Transform(context);

    for (i = 0; i < 8U; i++)
    {
        hash[i * 4U] = (uint8_t)(context->state[i] >> 24);
        hash[i * 4U + 1U] = (uint8_t)(context->state[i] >> 16);
        hash[i * 4U + 2U] = (uint8_t)(context->state[i] >> 8);
        hash[i * 4U + 3U] = (uint8_t)context->state[i];
    }
}

void BootSha256_Calculate(const uint8_t *data, uint32_t length, uint8_t hash[32])
{
    BootSha256Context_t context;
    BootSha256_Init(&context);
    BootSha256_Update(&context, data, length);
    BootSha256_Final(&context, hash);
}
