/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the broad phase, the sphere test, the gap and the
 * linked (parent) contact. */
#include "contact_internal.h"

enum {
    MODEL_CONTACT = 0x80,              /* target flags: contact by the model program */
    BOUNDS_DIFFER = 0x01, NEAR_SPHERE = 0x02, ALIGNED = 0x40, RESPONDED = 0x80,
    SLOW_MOTION = 0x258,               /* the largest velocity sum (|x|+|y|+|z|) that may align */
    RESTING = 0xffff,                  /* cargo_type of a record that may settle aligned */
    ALIGNED_DOT = 0x7800,              /* the least (unsigned) high word of the aligned dot product */
    EVENT_MODEL = 0x10,                /* models above this get the contact event */
    LINKED_EVENT = 0xffff,
    POSITION_BIAS = 0x37,
    WORD_LIMIT = 0x3f00
};

static uint32_t dword_of(const uint16_t w[2]) { return (uint32_t)w[0] | (uint32_t)w[1] << 16; }

static int bounds_differ(const fe2_object *t)
{
    return t->model_scale_b[1] != t->model_scale_a[1] || t->model_scale_b[0] != t->model_scale_a[0];
}

unsigned fe2_contact_word_shift(const int32_t v[3])
{
    static const unsigned order[3] = {2, 0, 1};
    unsigned count = 0, k;
    for (k = 0; k < 3; ++k) {
        uint32_t m = fe2_contact_magnitude((uint32_t)v[order[k]]) >> count;
        while (m >= WORD_LIMIT) {
            ++count;
            m >>= 1;
        }
    }
    return count;
}

uint32_t fe2_contact_square(uint32_t v)
{
    uint32_t m = fe2_contact_magnitude(v), hi = m >> 16, lo = m & 0xffffu;
    return hi * hi + (((lo * hi) << 1) >> 16);
}

/* A restoring division over 32 steps (the remainder's top bit lost on
 * each shift); the complemented borrows, halved. */
static uint32_t trial_quotient(uint32_t dividend, uint32_t divisor)
{
    uint32_t borrows = 0;
    unsigned i;
    for (i = 0; i < 32; ++i) {
        int borrow;
        dividend <<= 1;
        borrow = dividend < divisor;
        if (!borrow) dividend -= divisor;
        borrows = borrows << 1 | (uint32_t)borrow;
    }
    return ~borrows >> 1;
}

uint32_t fe2_contact_root(uint32_t s)
{
    uint32_t n = s << 1, g = (n >> 1) + UINT32_C(0x40000000);
    for (;;) {
        uint32_t q = trial_quotient(n, g);
        g = (g + q) >> 1;
        if ((int32_t)(q - g) >= -32768) return g;
    }
}

static int bit_length(uint32_t v)
{
    int n = 0;
    while (v) {
        ++n;
        v >>= 1;
    }
    return n;
}

/* Outside the sphere: the gap is the root of the squared distance less
 * the radius (at least 0), scaled back by the exponent; a doubling that
 * overflows leaves the distance as it was. */
static uint8_t outside(fe2_contact *c, const fe2_object *t, uint32_t squared, uint32_t radius, int16_t exponent)
{
    uint32_t gap = fe2_contact_root(squared) - radius;
    uint16_t n = (uint16_t)(exponent + 1);
    int publish = 1;
    c->sphere |= NEAR_SPHERE;
    if ((int32_t)gap < 0) gap = 0;
    for (; n != 0 && gap != 0; --n) {
        if (((gap >> 31) ^ (gap >> 30)) & 1) {
            publish = 0;
            break;
        }
        gap <<= 1;
    }
    if (publish) c->distance = gap;
    if (bounds_differ(t)) c->sphere |= BOUNDS_DIFFER;
    return c->sphere;
}

/* Inside the parent's sphere. A slow resting record whose second basis
 * column lies along its normalized position (or any direction giving a
 * negative dot: the test is unsigned) settles aligned; otherwise the
 * column takes the position's high words and the response flag. The
 * basis is rebuilt around that column, the motion stopped, and the record
 * put on the sphere: the scaled position rescaled to the summed model
 * radii (the target's +0x16 word plus the record's +0x18, aligned by the
 * scales +0xa + +0xc), then shifted back to 64 bits. */
static uint8_t linked(fe2_contact *c, const fe2_contact_services *game, fe2_object *o, fe2_object *t,
                      const int32_t scaled[3], uint32_t squared)
{
    int16_t direction[3];
    int32_t v[3];
    const uint8_t *target_model, *model;
    uint32_t sum = 0, multiplier, accumulator = squared;
    uint16_t exponent, high;
    unsigned i, n, count;
    int16_t bias;
    uint8_t flags;
    int aligned = 0;
    c->sphere = NEAR_SPHERE;
    for (i = 0; i < 3; ++i) sum += fe2_contact_magnitude((uint32_t)o->velocity[i]);
    if (sum <= SLOW_MOTION && o->cargo_type == RESTING) {
        uint32_t dot = 0;
        count = fe2_contact_word_shift(scaled);
        for (i = 0; i < 3; ++i) direction[i] = (int16_t)(uint16_t)(uint32_t)(scaled[i] >> count);
        fe2_motion_normalize(direction);
        for (i = 0; i < 3; ++i) dot += (uint32_t)((int32_t)o->orientation[1 + 3 * i] * direction[i]);
        if ((dot << 1) >> 16 >= ALIGNED_DOT) {
            c->sphere |= ALIGNED;
            c->respond_now = 0;
            aligned = 1;
        }
    }
    if (!aligned) {
        c->respond_now = c->response;
        for (i = 0; i < 3; ++i) direction[i] = (int16_t)(uint16_t)((uint32_t)scaled[i] >> 16);
    }
    for (i = 0; i < 3; ++i) o->orientation[1 + 3 * i] = direction[i];
    fe2_motion_swap_basis_columns(o->orientation);
    fe2_motion_rebuild_basis(o);
    fe2_motion_swap_basis_columns(o->orientation);

    target_model = game->model(game->opaque, t->model);
    exponent = (uint16_t)(fe2_contact_word(target_model + 0xa) + fe2_contact_word(target_model + 0xc));
    model = game->model(game->opaque, o->model);
    n = (uint16_t)(exponent - fe2_contact_word(model + 0xa) - fe2_contact_word(model + 0xc)) & 0x3fu;
    multiplier = n >= 32 ? 0 : ((uint32_t)fe2_contact_word(model + 0x18) << 16) >> n;
    high = (uint16_t)((multiplier >> 16) + fe2_contact_word(target_model + 0x16));
    multiplier = (uint32_t)high << 16 | (multiplier & 0xffffu);
    if (high & 0x8000u) {
        multiplier >>= 1;
        ++exponent;
    }
    for (i = 0; i < 3; ++i) v[i] = scaled[i];
    fe2_math_rescale(v, &accumulator, multiplier);
    for (i = 0; i < 3; ++i) o->velocity[i] = 0;
    /* (v:0) shifted right 0x37 - exponent + 1 times (the count is a word
     * counted down to below zero). */
    bias = (int16_t)(uint16_t)(POSITION_BIAS - exponent);
    count = bias >= 0 ? (unsigned)bias + 1u : bias == INT16_MIN ? 0x8001u : 1u;
    if (count > 63) count = 63;
    for (i = 0; i < 3; ++i) {
        int64_t p = (int64_t)((uint64_t)(uint32_t)v[i] << 32) >> count;
        o->position_low[i] = (uint32_t)(uint64_t)p;
        o->position_high[i] = (uint32_t)((uint64_t)p >> 32);
    }
    if (c->respond_now != 0) {
        c->sphere |= RESPONDED;
        if (o->model > EVENT_MODEL) game->collision(game->opaque, o, t, LINKED_EVENT, 0);
    }
    flags = c->sphere;
    if (bounds_differ(t)) flags |= BOUNDS_DIFFER;
    if (c->handle == 0) {
        c->handle = t->handle;
        c->reported = t;
    }
    c->sphere = flags;
    return flags;
}

/*
 * The broad phase: half the sum of the absolute components, less the
 * record's bound (+0x10a, 32 bits) and the target's (48 bits), must be
 * negative. A target with model contact (+0x118 bit 7) runs its program.
 * Otherwise the components and the summed radius (the target's bound plus
 * the record's +0x110) are scaled to dwords by a common shift: down until
 * the high dwords' envelope (with the target's third bound word, doubled)
 * is clear, or up until the low envelope's top bit is set. A low envelope
 * with its top bit set, or zero, takes a single shift down. The squared
 * distance and radius (approximate, wrapping) then decide outside (the
 * gap), the general response, or the linked contact (the target is the
 * record's parent).
 */
uint8_t fe2_contact_classify(fe2_contact *c, const fe2_contact_services *game, fe2_object *o, fe2_object *t,
                             const int64_t relative[3])
{
    uint64_t magnitude[3], v;
    uint32_t envelope, radius, squared;
    int32_t scaled[3];
    int16_t exponent;
    unsigned i;
    for (i = 0; i < 3; ++i) magnitude[i] = relative[i] < 0 ? 0u - (uint64_t)relative[i] : (uint64_t)relative[i];
    v = ((magnitude[0] + magnitude[1] + magnitude[2]) >> 1) - dword_of(o->model_scale_a);
    if ((int64_t)v >= 0) {
        uint64_t bound = (uint64_t)dword_of(t->model_scale_a) | (uint64_t)t->model_scale_a[2] << 32;
        if ((int64_t)(v - bound) >= 0) return 0;
    }
    if (t->flags & MODEL_CONTACT) return fe2_contact_model(c, game, o, t, relative);

    envelope = t->model_scale_a[2];
    for (i = 0; i < 3; ++i) envelope |= (uint32_t)(magnitude[i] >> 32);
    if (envelope == 0) {
        envelope = dword_of(t->model_scale_a);
        for (i = 0; i < 3; ++i) envelope |= (uint32_t)magnitude[i];
        if (envelope != 0 && !(envelope & 0x80000000u)) {
            unsigned up = (unsigned)(31 - bit_length(envelope));
            for (i = 0; i < 3; ++i) scaled[i] = (int32_t)((uint32_t)relative[i] << up);
            radius = (dword_of(t->model_scale_a) + dword_of(o->model_scale_b)) << up;
            exponent = (int16_t)(1 - (int)up);
            goto measure;
        }
        envelope = 0;
    } else {
        envelope <<= 1;
    }
    {
        int down = bit_length(envelope);
        uint64_t wide = ((uint64_t)dword_of(t->model_scale_a) | (uint64_t)t->model_scale_a[2] << 32) +
                        dword_of(o->model_scale_b);
        if (down == 0) down = 1;
        for (i = 0; i < 3; ++i) scaled[i] = (int32_t)(uint32_t)(uint64_t)(relative[i] >> down);
        radius = (uint32_t)(wide >> down);
        exponent = (int16_t)(down - 1);
    }
measure:
    squared = 0;
    for (i = 0; i < 3; ++i) squared += fe2_contact_square((uint32_t)scaled[i]);
    if (squared >= fe2_contact_square(radius) + 1u) return outside(c, t, squared, radius, exponent);
    if (t->handle != o->parent_handle) return fe2_contact_respond(c, game, o, t);
    return linked(c, game, o, t, scaled, squared);
}
