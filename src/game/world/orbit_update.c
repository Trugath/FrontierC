/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the orbit update. */
#include "orbits.h"

enum {
    OLD_SAMPLE = 0, NEW_SAMPLE = 2, MULTIPLIER = 4,
    COORDINATE_PARAMETER = 0, ACCUMULATOR_PARAMETER = 1, SCALE_PARAMETER = 6,
    EXPONENT_PARAMETER = 7,
    RAMP_RISING_BIT = 0x01, RAMP_FLOOR = 0xfe
};

static uint64_t words64(const uint16_t w[4])
{
    return (uint64_t)w[3] << 48 | (uint64_t)w[2] << 32 | (uint32_t)w[1] << 16 | w[0];
}

static void store64(uint16_t w[4], uint64_t value)
{
    unsigned i;
    for (i = 0; i < 4; ++i) w[i] = (uint16_t)(value >> (16 * i));
}

static void sample_args(fe2_call_args *args, unsigned n, uint64_t value)
{
    fe2_args_dword(args, n, (uint32_t)value);
    fe2_args_dword(args, n + 1, (uint32_t)(value >> 32));
}

/* Re-interpolate the position from the orbit phase and the accumulator. */
static void interpolate(const fe2_orbit_services *game, fe2_object *o)
{
    fe2_call_args args;
    fe2_args_clear(&args);
    fe2_args_word(&args, COORDINATE_PARAMETER, o->as.body.orbit_inclination);
    fe2_args_dword(&args, ACCUMULATOR_PARAMETER,
                   (uint32_t)o->as.body.orbit_product[3] << 16 | o->as.body.orbit_product[2]);
    fe2_args_word(&args, SCALE_PARAMETER, (uint16_t)o->scalar);
    fe2_args_word(&args, EXPONENT_PARAMETER, (uint16_t)(o->scalar >> 16));
    game->interpolate(game->opaque, o, &args);
}

/*
 * Advance the orbit accumulator. Its bits 36..63 are the position bucket:
 * - an unchanged bucket keeps the position;
 * - a bucket that grows within the same high word moves the position on by
 *   the difference;
 * - anything else re-interpolates.
 */
static void advance_orbit(const fe2_orbit_services *game, fe2_object *o, uint64_t previous,
                          uint64_t sample)
{
    fe2_call_args args;
    uint16_t product[4];
    uint64_t old_accumulator = words64(o->as.body.orbit_product), accumulator;
    uint32_t old_bucket, new_bucket;
    fe2_args_clear(&args);
    sample_args(&args, OLD_SAMPLE, previous);
    sample_args(&args, NEW_SAMPLE, sample);
    fe2_args_dword(&args, MULTIPLIER, o->as.body.orbit_radius);
    game->wide_product(game->opaque, &args, product);
    accumulator = old_accumulator + words64(product);
    store64(o->as.body.orbit_product, accumulator);
    old_bucket = (uint32_t)(old_accumulator >> 36);
    new_bucket = (uint32_t)(accumulator >> 36);
    if (old_bucket == new_bucket) return;
    if ((old_bucket >> 16) == (new_bucket >> 16) && (uint16_t)new_bucket >= (uint16_t)old_bucket)
        game->scale_vector(game->opaque, o, (uint16_t)(new_bucket - old_bucket));
    else interpolate(game, o);
}

/* The scalar (+0x74) ramps by half the frame step: up to 0xffff (clearing
 * control bit 0), or down to 0 (control becomes 0xfe). Control 0 holds it. */
static void ramp(const fe2_orbit_state *s, fe2_object *o)
{
    int8_t control = (int8_t)o->as.body.docking_state;
    uint32_t step = s->frame_step >> 1;
    if (control == 0) return;
    if (control > 0) {
        uint32_t value = o->cargo_type + step;
        if (value < o->cargo_type || value >= 0xffffu) {
            o->as.body.docking_state &= (uint8_t)~RAMP_RISING_BIT;
            value = 0xffff;
        }
        o->cargo_type = (uint16_t)value;
    } else if (o->cargo_type < step) {
        o->cargo_type = 0;
        o->as.body.docking_state = RAMP_FLOOR;
    } else {
        o->cargo_type = (uint16_t)(o->cargo_type - step);
    }
}

/*
 * Re-orient when the time bucket changes: both samples' low dwords shift
 * left by (time shift - 1) and are compared in bits 20..31. The original
 * pops the saved old dword in the order it pushed it, so the old sample's
 * words arrive swapped. The new
 * sample's shifted high word and the basis seed (+0xa2) are the basis
 * angles, swapped by FE2_ORBIT_SWAP_BASIS. Returns nonzero when the
 * swapped basis was built (that path continues into the ramp).
 */
static int reorient(fe2_orbit_state *s, const fe2_orbit_services *game, fe2_object *o,
                    uint32_t previous)
{
    uint32_t current = o->orbit[0];
    unsigned shift = o->as.body.time_shift;
    if (shift == 0) return 0;
    for (; shift > 1; --shift) {
        previous <<= 1;
        current <<= 1;
    }
    if ((previous >> 20) == (current >> 20)) return 0;
    if (o->flags & FE2_ORBIT_SWAP_BASIS) {
        s->basis_angle[0] = (uint16_t)(current >> 16);
        s->basis_angle[1] = o->as.body.basis_seed;
    } else {
        s->basis_angle[0] = o->as.body.basis_seed;
        s->basis_angle[1] = (uint16_t)(current >> 16);
    }
    game->basis_b(game->opaque, o);
    return (o->flags & FE2_ORBIT_SWAP_BASIS) != 0;
}

void fe2_orbit_update(fe2_orbit_state *s, const fe2_orbit_services *game, fe2_object *o,
                      uint64_t sample)
{
    uint64_t previous = (uint64_t)o->orbit[1] << 32 | o->orbit[0];
    o->orbit[0] = (uint32_t)sample;
    o->orbit[1] = (uint32_t)(sample >> 32);
    if (!(o->flags & FE2_ORBIT_FIXED)) {
        advance_orbit(game, o, previous, sample);
        if (!reorient(s, game, o, (uint32_t)previous << 16 | (uint32_t)previous >> 16)) return;
    }
    ramp(s, o);
}
