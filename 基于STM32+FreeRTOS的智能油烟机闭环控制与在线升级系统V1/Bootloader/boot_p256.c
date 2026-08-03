#include "boot_p256.h"
#include <string.h>

/*
 * Small, verification-only NIST P-256 implementation.
 * Arithmetic deliberately favours auditability and code size over speed:
 * modular multiplication uses constant-width double-and-add, while EC points
 * use Jacobian coordinates so only two modular inversions are required.
 */
#define P256_WORDS 8U

typedef struct { uint32_t v[P256_WORDS]; } U256;
typedef struct { U256 x; U256 y; } AffinePoint;
typedef struct { U256 x; U256 y; U256 z; } JacobianPoint;

static const U256 s_p = {{
    0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0x00000000UL,
    0x00000000UL, 0x00000000UL, 0x00000001UL, 0xFFFFFFFFUL
}};
static const U256 s_n = {{
    0xFC632551UL, 0xF3B9CAC2UL, 0xA7179E84UL, 0xBCE6FAADUL,
    0xFFFFFFFFUL, 0xFFFFFFFFUL, 0x00000000UL, 0xFFFFFFFFUL
}};
static const U256 s_b = {{
    0x27D2604BUL, 0x3BCE3C3EUL, 0xCC53B0F6UL, 0x651D06B0UL,
    0x769886BCUL, 0xB3EBBD55UL, 0xAA3A93E7UL, 0x5AC635D8UL
}};
static const AffinePoint s_g = {
    {{0xD898C296UL, 0xF4A13945UL, 0x2DEB33A0UL, 0x77037D81UL,
      0x63A440F2UL, 0xF8BCE6E5UL, 0xE12C4247UL, 0x6B17D1F2UL}},
    {{0x37BF51F5UL, 0xCBB64068UL, 0x6B315ECEUL, 0x2BCE3357UL,
      0x7C0F9E16UL, 0x8EE7EB4AUL, 0xFE1A7F9BUL, 0x4FE342E2UL}}
};

static void U256_Clear(U256 *a) { memset(a, 0, sizeof(*a)); }
static void U256_Copy(U256 *dst, const U256 *src) { memcpy(dst, src, sizeof(*dst)); }

static int U256_IsZero(const U256 *a)
{
    uint32_t value = 0U;
    uint32_t i;
    for (i = 0; i < P256_WORDS; i++) { value |= a->v[i]; }
    return value == 0U;
}

static int U256_Compare(const U256 *a, const U256 *b)
{
    int i;
    for (i = (int)P256_WORDS - 1; i >= 0; i--)
    {
        if (a->v[i] > b->v[i]) { return 1; }
        if (a->v[i] < b->v[i]) { return -1; }
    }
    return 0;
}

static uint32_t U256_AddRaw(U256 *r, const U256 *a, const U256 *b)
{
    uint64_t sum;
    uint32_t carry = 0U;
    uint32_t i;
    for (i = 0; i < P256_WORDS; i++)
    {
        sum = (uint64_t)a->v[i] + b->v[i] + carry;
        r->v[i] = (uint32_t)sum;
        carry = (uint32_t)(sum >> 32);
    }
    return carry;
}

static uint32_t U256_SubRaw(U256 *r, const U256 *a, const U256 *b)
{
    uint64_t sub;
    uint32_t borrow = 0U;
    uint32_t i;
    for (i = 0; i < P256_WORDS; i++)
    {
        sub = (uint64_t)b->v[i] + borrow;
        r->v[i] = (uint32_t)((uint64_t)a->v[i] - sub);
        borrow = ((uint64_t)a->v[i] < sub) ? 1U : 0U;
    }
    return borrow;
}

static void U256_ModAdd(U256 *r, const U256 *a, const U256 *b, const U256 *m)
{
    U256 m_minus_b;
    U256 result;

    /*
     * Avoid reducing a truncated 256-bit sum after carry. Express the same
     * operation as a - (m - b) when a + b would cross the modulus; otherwise
     * the raw addition cannot overflow because a < m - b.
     */
    (void)U256_SubRaw(&m_minus_b, m, b);
    if (U256_Compare(a, &m_minus_b) >= 0)
    {
        (void)U256_SubRaw(&result, a, &m_minus_b);
    }
    else
    {
        (void)U256_AddRaw(&result, a, b);
    }
    U256_Copy(r, &result);
}

static void U256_ModSub(U256 *r, const U256 *a, const U256 *b, const U256 *m)
{
    U256 result;
    if (U256_Compare(a, b) >= 0)
    {
        (void)U256_SubRaw(&result, a, b);
    }
    else
    {
        U256 delta;
        (void)U256_SubRaw(&delta, b, a);
        (void)U256_SubRaw(&result, m, &delta);
    }
    U256_Copy(r, &result);
}

static uint32_t U256_GetBit(const U256 *a, uint32_t bit)
{
    return (a->v[bit >> 5] >> (bit & 31U)) & 1U;
}

static void U256_ModMul(U256 *r, const U256 *a, const U256 *b, const U256 *m)
{
    U256 result;
    U256 addend;
    uint32_t i;

    U256_Clear(&result);
    U256_Copy(&addend, a);
    if (U256_Compare(&addend, m) >= 0) { (void)U256_SubRaw(&addend, &addend, m); }

    for (i = 0; i < 256U; i++)
    {
        if (U256_GetBit(b, i) != 0U)
        {
            U256_ModAdd(&result, &result, &addend, m);
        }
        U256_ModAdd(&addend, &addend, &addend, m);
    }
    U256_Copy(r, &result);
}

static void U256_ModSquare(U256 *r, const U256 *a, const U256 *m)
{
    U256_ModMul(r, a, a, m);
}

static void U256_ModInverse(U256 *r, const U256 *a, const U256 *m)
{
    U256 exponent;
    U256 result;
    U256 base;
    U256 two;
    int i;

    U256_Clear(&two); two.v[0] = 2U;
    (void)U256_SubRaw(&exponent, m, &two);
    U256_Clear(&result); result.v[0] = 1U;
    U256_Copy(&base, a);

    for (i = 255; i >= 0; i--)
    {
        U256_ModSquare(&result, &result, m);
        if (U256_GetBit(&exponent, (uint32_t)i) != 0U)
        {
            U256_ModMul(&result, &result, &base, m);
        }
    }
    U256_Copy(r, &result);
}

static void U256_FromBigEndian(U256 *r, const uint8_t bytes[32])
{
    uint32_t word;
    uint32_t i;
    for (i = 0; i < P256_WORDS; i++)
    {
        uint32_t o = 28U - i * 4U;
        word = ((uint32_t)bytes[o] << 24) |
               ((uint32_t)bytes[o + 1U] << 16) |
               ((uint32_t)bytes[o + 2U] << 8) |
               bytes[o + 3U];
        r->v[i] = word;
    }
}

static void Point_SetInfinity(JacobianPoint *p)
{
    U256_Clear(&p->x); U256_Clear(&p->y); U256_Clear(&p->z);
}

static int Point_IsInfinity(const JacobianPoint *p) { return U256_IsZero(&p->z); }

static void Point_Double(JacobianPoint *r, const JacobianPoint *p)
{
    U256 delta, gamma, beta, alpha, t1, t2, x3, y3, z3;

    if (Point_IsInfinity(p) || U256_IsZero(&p->y)) { Point_SetInfinity(r); return; }
    U256_ModSquare(&delta, &p->z, &s_p);
    U256_ModSquare(&gamma, &p->y, &s_p);
    U256_ModMul(&beta, &p->x, &gamma, &s_p);
    U256_ModSub(&t1, &p->x, &delta, &s_p);
    U256_ModAdd(&t2, &p->x, &delta, &s_p);
    U256_ModMul(&alpha, &t1, &t2, &s_p);
    U256_ModAdd(&t1, &alpha, &alpha, &s_p);
    U256_ModAdd(&alpha, &t1, &alpha, &s_p);             /* 3 * alpha */
    U256_ModSquare(&x3, &alpha, &s_p);
    U256_ModAdd(&t1, &beta, &beta, &s_p);
    U256_ModAdd(&t1, &t1, &t1, &s_p);
    U256_ModAdd(&t1, &t1, &t1, &s_p);                 /* 8 * beta */
    U256_ModSub(&x3, &x3, &t1, &s_p);
    U256_ModAdd(&z3, &p->y, &p->z, &s_p);
    U256_ModSquare(&z3, &z3, &s_p);
    U256_ModSub(&z3, &z3, &gamma, &s_p);
    U256_ModSub(&z3, &z3, &delta, &s_p);
    U256_ModAdd(&t1, &beta, &beta, &s_p);
    U256_ModAdd(&t1, &t1, &t1, &s_p);                 /* 4 * beta */
    U256_ModSub(&t1, &t1, &x3, &s_p);
    U256_ModMul(&y3, &alpha, &t1, &s_p);
    U256_ModSquare(&t2, &gamma, &s_p);
    U256_ModAdd(&t2, &t2, &t2, &s_p);
    U256_ModAdd(&t2, &t2, &t2, &s_p);
    U256_ModAdd(&t2, &t2, &t2, &s_p);                 /* 8 * gamma^2 */
    U256_ModSub(&y3, &y3, &t2, &s_p);
    U256_Copy(&r->x, &x3); U256_Copy(&r->y, &y3); U256_Copy(&r->z, &z3);
}

static void Point_AddMixed(JacobianPoint *r, const JacobianPoint *p, const AffinePoint *q)
{
    U256 z2, u2, s2, h, hh, i4, j, rr, v, t, x3, y3, z3;

    if (Point_IsInfinity(p))
    {
        U256_Copy(&r->x, &q->x); U256_Copy(&r->y, &q->y);
        U256_Clear(&r->z); r->z.v[0] = 1U; return;
    }
    U256_ModSquare(&z2, &p->z, &s_p);
    U256_ModMul(&u2, &q->x, &z2, &s_p);
    U256_ModMul(&s2, &q->y, &p->z, &s_p);
    U256_ModMul(&s2, &s2, &z2, &s_p);
    U256_ModSub(&h, &u2, &p->x, &s_p);
    if (U256_IsZero(&h))
    {
        if (U256_Compare(&s2, &p->y) == 0) { Point_Double(r, p); }
        else { Point_SetInfinity(r); }
        return;
    }
    U256_ModSquare(&hh, &h, &s_p);
    U256_ModAdd(&i4, &hh, &hh, &s_p); U256_ModAdd(&i4, &i4, &i4, &s_p);
    U256_ModMul(&j, &h, &i4, &s_p);
    U256_ModSub(&rr, &s2, &p->y, &s_p); U256_ModAdd(&rr, &rr, &rr, &s_p);
    U256_ModMul(&v, &p->x, &i4, &s_p);
    U256_ModSquare(&x3, &rr, &s_p);
    U256_ModSub(&x3, &x3, &j, &s_p);
    U256_ModAdd(&t, &v, &v, &s_p); U256_ModSub(&x3, &x3, &t, &s_p);
    U256_ModSub(&t, &v, &x3, &s_p); U256_ModMul(&y3, &rr, &t, &s_p);
    U256_ModMul(&t, &p->y, &j, &s_p); U256_ModAdd(&t, &t, &t, &s_p);
    U256_ModSub(&y3, &y3, &t, &s_p);
    U256_ModAdd(&z3, &p->z, &h, &s_p); U256_ModSquare(&z3, &z3, &s_p);
    U256_ModSub(&z3, &z3, &z2, &s_p); U256_ModSub(&z3, &z3, &hh, &s_p);
    U256_Copy(&r->x, &x3); U256_Copy(&r->y, &y3); U256_Copy(&r->z, &z3);
}

static int Point_ToAffine(AffinePoint *r, const JacobianPoint *p)
{
    U256 zi, zi2, zi3;
    if (Point_IsInfinity(p)) { return 0; }
    U256_ModInverse(&zi, &p->z, &s_p);
    U256_ModSquare(&zi2, &zi, &s_p);
    U256_ModMul(&zi3, &zi2, &zi, &s_p);
    U256_ModMul(&r->x, &p->x, &zi2, &s_p);
    U256_ModMul(&r->y, &p->y, &zi3, &s_p);
    return 1;
}

static void Point_ScalarMultiply(JacobianPoint *r, const AffinePoint *p, const U256 *scalar)
{
    int bit;
    JacobianPoint result;
    Point_SetInfinity(&result);
    for (bit = 255; bit >= 0; bit--)
    {
        Point_Double(&result, &result);
        if (U256_GetBit(scalar, (uint32_t)bit) != 0U)
        {
            Point_AddMixed(&result, &result, p);
        }
    }
    memcpy(r, &result, sizeof(*r));
}

static int Point_IsValid(const AffinePoint *q)
{
    U256 y2, x2, x3, three_x, rhs;
    if (U256_IsZero(&q->x) && U256_IsZero(&q->y)) { return 0; }
    if (U256_Compare(&q->x, &s_p) >= 0 || U256_Compare(&q->y, &s_p) >= 0) { return 0; }
    U256_ModSquare(&y2, &q->y, &s_p);
    U256_ModSquare(&x2, &q->x, &s_p); U256_ModMul(&x3, &x2, &q->x, &s_p);
    U256_ModAdd(&three_x, &q->x, &q->x, &s_p);
    U256_ModAdd(&three_x, &three_x, &q->x, &s_p);
    U256_ModSub(&rhs, &x3, &three_x, &s_p); U256_ModAdd(&rhs, &rhs, &s_b, &s_p);
    return U256_Compare(&y2, &rhs) == 0;
}

int BootP256_Verify(const uint8_t public_key[64],
                    const uint8_t hash[32],
                    const uint8_t signature[64])
{
    U256 r_sig, s_sig, z, w, u1, u2, x_mod_n;
    AffinePoint q, p2_affine, sum_affine;
    JacobianPoint p1, p2, sum;

    U256_FromBigEndian(&q.x, public_key); U256_FromBigEndian(&q.y, public_key + 32);
    U256_FromBigEndian(&r_sig, signature); U256_FromBigEndian(&s_sig, signature + 32);
    U256_FromBigEndian(&z, hash);
    if (!Point_IsValid(&q) || U256_IsZero(&r_sig) || U256_IsZero(&s_sig) ||
        U256_Compare(&r_sig, &s_n) >= 0 || U256_Compare(&s_sig, &s_n) >= 0)
    {
        return 0;
    }
    if (U256_Compare(&z, &s_n) >= 0) { (void)U256_SubRaw(&z, &z, &s_n); }
    U256_ModInverse(&w, &s_sig, &s_n);
    U256_ModMul(&u1, &z, &w, &s_n);
    U256_ModMul(&u2, &r_sig, &w, &s_n);
    Point_ScalarMultiply(&p1, &s_g, &u1);
    Point_ScalarMultiply(&p2, &q, &u2);
    if (!Point_ToAffine(&p2_affine, &p2)) { return 0; }
    Point_AddMixed(&sum, &p1, &p2_affine);
    if (!Point_ToAffine(&sum_affine, &sum)) { return 0; }
    U256_Copy(&x_mod_n, &sum_affine.x);
    if (U256_Compare(&x_mod_n, &s_n) >= 0) { (void)U256_SubRaw(&x_mod_n, &x_mod_n, &s_n); }
    return U256_Compare(&x_mod_n, &r_sig) == 0;
}
