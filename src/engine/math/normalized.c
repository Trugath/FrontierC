/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* normalized scalar arithmetic and conversions. */
#include "math3.h"

/* Top two bits equal: one more left shift keeps the sign. */
static int has_headroom16(uint16_t m) { return ((m ^ (m << 1)) & 0x8000u) == 0; }
static int has_headroom32(uint32_t m) { return ((m ^ (m << 1)) & UINT32_C(0x80000000)) == 0; }

/* x86 SAR/SHR on a 16-bit operand: the count is masked to 5 bits, and
 * counts of 16 or more leave only sign (or zero) bits. */
static uint16_t sar16(uint16_t value, unsigned count)
{
    count &= 31;
    if (count > 15) count = 15;
    return (uint16_t)((int16_t)value >> count);
}

static uint16_t shr16(uint16_t value, unsigned count)
{
    count &= 31;
    return count > 15 ? 0 : (uint16_t)(value >> count);
}

static uint32_t sar32(uint32_t value, unsigned count)
{
    if (count > 31) count = 31;
    return (uint32_t)((int32_t)value >> count);
}

fe2_scalar fe2_math_normalize(fe2_scalar v)
{
    if (v.mantissa == 0) return v;
    while (has_headroom16(v.mantissa)) {
        v.mantissa = (uint16_t)(v.mantissa << 1);
        --v.exponent;
    }
    return v;
}

/* Shift until bit 15 is reached, then step back: the highest bit lands on
 * bit 14 (for mantissas below 0x8000). */
fe2_scalar fe2_math_normalize_unsigned(fe2_scalar v)
{
    if (v.mantissa == 0) return v;
    do {
        v.mantissa = (uint16_t)(v.mantissa << 1);
        --v.exponent;
    } while (!(v.mantissa & 0x8000u));
    v.mantissa >>= 1;
    ++v.exponent;
    return v;
}

fe2_scalar fe2_math_normalize_positive(fe2_scalar v)
{
    if (v.mantissa == 0 || (v.mantissa & 0x8000u)) return v;
    while (!(v.mantissa & 0x8000u)) {
        v.mantissa = (uint16_t)(v.mantissa << 1);
        --v.exponent;
    }
    return v;
}

fe2_scalar32 fe2_math_normalize32(fe2_scalar32 v)
{
    if (v.mantissa == 0) return v;
    while (has_headroom32(v.mantissa)) {
        v.mantissa <<= 1;
        --v.exponent;
    }
    return v;
}

/* Align the smaller exponent's mantissa (an arithmetic shift with the x86
 * count masking), add, then halve on overflow or normalize. */
fe2_scalar fe2_math_add(fe2_scalar a, fe2_scalar b)
{
    uint16_t sum;
    uint32_t wide;
    if ((int16_t)b.exponent > (int16_t)a.exponent) {
        fe2_scalar t = a;
        a = b;
        b = t;
    }
    b.mantissa = sar16(b.mantissa, (uint8_t)(a.exponent - b.exponent));
    wide = (uint32_t)a.mantissa + b.mantissa;
    sum = (uint16_t)wide;
    if (((a.mantissa ^ sum) & (b.mantissa ^ sum)) & 0x8000u) {
        a.mantissa = (uint16_t)((sum >> 1) | ((wide >> 16) << 15));
        ++a.exponent;
        return a;
    }
    a.mantissa = sum;
    return fe2_math_normalize(a);
}

fe2_scalar fe2_math_subtract(fe2_scalar a, fe2_scalar b)
{
    b.mantissa = (uint16_t)-b.mantissa;
    return fe2_math_add(a, b);
}

fe2_scalar32 fe2_math_add32(fe2_scalar32 a, fe2_scalar32 b)
{
    uint32_t sum;
    uint16_t gap;
    if ((int16_t)b.exponent > (int16_t)a.exponent) {
        fe2_scalar32 t = a;
        a = b;
        b = t;
    }
    gap = (uint16_t)(a.exponent - b.exponent);       /* a one-bit loop of `gap` steps */
    b.mantissa = sar32(b.mantissa, gap);
    sum = a.mantissa + b.mantissa;
    if (((a.mantissa ^ sum) & (b.mantissa ^ sum)) & UINT32_C(0x80000000)) {
        uint32_t carry = sum < a.mantissa;
        a.mantissa = (sum >> 1) | (carry << 31);
        ++a.exponent;
        return a;
    }
    a.mantissa = sum;
    return fe2_math_normalize32(a);
}

fe2_scalar32 fe2_math_subtract32(fe2_scalar32 a, fe2_scalar32 b)
{
    b.mantissa = 0u - b.mantissa;
    return fe2_math_add32(a, b);
}

/* Nonnegative mantissas: a logical alignment, and a sum reaching bit 15 is
 * halved (cancellation is not normalized). */
fe2_scalar fe2_math_add_unsigned(fe2_scalar a, fe2_scalar b)
{
    if ((int16_t)b.exponent > (int16_t)a.exponent) {
        fe2_scalar t = a;
        a = b;
        b = t;
    }
    a.mantissa = (uint16_t)(a.mantissa + shr16(b.mantissa, (uint8_t)(a.exponent - b.exponent)));
    if (a.mantissa & 0x8000u) {
        a.mantissa >>= 1;
        ++a.exponent;
    }
    return a;
}

fe2_scalar32 fe2_math_multiply(fe2_scalar a, fe2_scalar b)
{
    fe2_scalar32 r;
    r.mantissa = (uint32_t)((int32_t)(int16_t)a.mantissa * (int16_t)b.mantissa) << 1;
    r.exponent = (uint16_t)(a.exponent + b.exponent);
    return r;
}

/* A quotient with headroom is doubled (its exponent kept), else the
 * exponent rises by one. */
fe2_scalar fe2_math_divide(fe2_scalar dividend, fe2_scalar divisor)
{
    fe2_scalar q;
    int8_t high;
    if (divisor.mantissa == 0) {
        q.mantissa = divisor.exponent;
        q.exponent = (uint16_t)(dividend.exponent + 1);
        return q;
    }
    q.exponent = (uint16_t)(dividend.exponent - divisor.exponent);
    q.mantissa = (uint16_t)(int16_t)(((int32_t)(int16_t)dividend.mantissa * 65536 / 4) /
                                     (int16_t)divisor.mantissa);
    high = (int8_t)(q.mantissa >> 8);
    if (high < 0x40 && high > -0x40) {
        q.mantissa = (uint16_t)(q.mantissa << 1);
        --q.exponent;
    }
    ++q.exponent;
    return q;
}

fe2_scalar fe2_math_divide32(int32_t dividend, int16_t divisor, uint16_t exponent)
{
    fe2_scalar q;
    q.exponent = (uint16_t)(exponent - (uint16_t)dividend);
    q.mantissa = (uint16_t)(int16_t)((dividend >> 2) / divisor);
    return q;
}

/* From a 32-bit value: shift to the top, keep the high word; an unsigned
 * mantissa is then halved. Zero gives exponent -7. */
fe2_scalar fe2_math_from_unsigned32(uint32_t value)
{
    fe2_scalar r;
    r.exponent = 0xfff9u;
    if (value == 0) {
        r.mantissa = 0;
        return r;
    }
    while (!(value & UINT32_C(0x80000000))) {
        value <<= 1;
        --r.exponent;
    }
    r.exponent = (uint16_t)(r.exponent + 0x27);
    r.mantissa = (uint16_t)(value >> 17);
    return r;
}

fe2_scalar fe2_math_from_signed32(int32_t value)
{
    uint32_t v = (uint32_t)value;
    fe2_scalar r;
    r.exponent = 0xfff9u;
    r.mantissa = 0;
    if (v == 0) return r;
    while (has_headroom32(v)) {
        v <<= 1;
        --r.exponent;
    }
    r.exponent = (uint16_t)(r.exponent + 0x26);
    r.mantissa = (uint16_t)(v >> 16);
    return r;
}

fe2_scalar32 fe2_math_normalize_signed32(int32_t value)
{
    fe2_scalar32 r;
    r.mantissa = (uint32_t)value;
    r.exponent = 0xfffau;
    if (r.mantissa == 0) return r;
    while (has_headroom32(r.mantissa)) {
        r.mantissa <<= 1;
        --r.exponent;
    }
    --r.exponent;
    return r;
}

/* mantissa << 16 shifted right by 31 - exponent; above that, the extreme. */
int32_t fe2_math_to_signed32(fe2_scalar v)
{
    uint16_t shift = (uint16_t)(0x1f - v.exponent);
    if ((int16_t)shift < 0) return (v.mantissa & 0x8000u) ? INT32_MIN : INT32_MAX;
    return (int32_t)sar32((uint32_t)v.mantissa << 16, shift);
}

int32_t fe2_math_to_signed32_unchecked(fe2_scalar v)
{
    return (int32_t)sar32((uint32_t)v.mantissa << 16, (uint16_t)(0x1f - v.exponent));
}

uint32_t fe2_math_to_unsigned32(fe2_scalar v)
{
    uint16_t shift = (uint16_t)(0x20 - v.exponent);
    uint32_t value = (uint32_t)(uint16_t)(v.mantissa << 1) << 16;
    if ((int16_t)shift < 0) return UINT32_MAX;
    return shift > 31 ? 0 : value >> shift;
}

int64_t fe2_math_to_signed64(fe2_scalar v)
{
    int16_t count = (int16_t)(0x3e - v.exponent);
    unsigned shift = count < 0 ? 1u : (unsigned)count + 1u;
    uint64_t value = (uint64_t)v.mantissa << 48;
    if (shift > 63) shift = 63;
    return (int64_t)(value >> shift) | ((v.mantissa & 0x8000u) ? (int64_t)~(UINT64_MAX >> shift) : 0);
}

int16_t fe2_math_to_signed16(fe2_scalar v)
{
    uint16_t shift = (uint16_t)(0x0f - v.exponent);
    if ((int16_t)shift < 0) return (v.mantissa & 0x8000u) ? INT16_MIN : INT16_MAX;
    return (int16_t)sar16(v.mantissa, (uint8_t)shift);
}
