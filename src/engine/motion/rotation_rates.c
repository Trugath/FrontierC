/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* rotation rates from controls, steering and a reference;
 * 0x10f9a the radial rate. */
#include "motion.h"

enum { RATE_LIMIT = 0x4000, LIMIT_SIGNS_SLOT = 1, RADIAL_SHIFT = 2 };

void fe2_motion_rates_from_controls(fe2_object *o)
{
    o->rotation_rates[2] = (int16_t)(o->controls[0] >> 3);
    o->rotation_rates[0] = (int16_t)(uint16_t)-(uint16_t)(int16_t)(o->controls[1] >> 2);
}

/* The 32-bit wrapped dot product of a word vector with basis column k. */
static uint32_t dot_column(const int16_t v[3], const fe2_object *o, unsigned k)
{
    return (uint32_t)((int32_t)v[0] * o->orientation[k]) + (uint32_t)((int32_t)v[1] * o->orientation[3 + k]) +
           (uint32_t)((int32_t)v[2] * o->orientation[6 + k]);
}

static int16_t fraction(uint32_t sum) { return (int16_t)(uint16_t)((sum << 1) >> 16); }

/* Steering toward the feedback direction: the first rate from column 1,
 * the third from column 0. Behind (column 2 negative) both are set to the
 * limit, whose signs the original reads at fixed offsets in the record's
 * segment: those are the bytes of the pool's slot 1 record at +0x11 and
 * +0x15 (the high bytes of its last basis word and first camera offset).
 * The port reads the current pool's. */
void fe2_motion_rates_from_steering(const fe2_motion *m, fe2_object *o)
{
    const fe2_object *signs;
    o->rotation_rates[0] = (int16_t)(uint16_t)-(uint16_t)fraction(dot_column(o->steering, o, 1));
    o->rotation_rates[2] = fraction(dot_column(o->steering, o, 0));
    o->rotation_rates[1] = 0;
    if (!(dot_column(o->steering, o, 2) & UINT32_C(0x80000000))) return;
    signs = fe2_pool_record(m->pool, LIMIT_SIGNS_SLOT);
    o->rotation_rates[0] = signs->orientation[8] < 0 ? -RATE_LIMIT : RATE_LIMIT;
    o->rotation_rates[2] = (signs->camera_offset[0] & 0x8000u) ? -RATE_LIMIT : RATE_LIMIT;
}

/* Turning away; ahead (column 2 not negative) each rate becomes its high
 * byte, sign-extended, XOR 0x3fff. */
void fe2_motion_rates_from_reversed_steering(fe2_object *o)
{
    o->rotation_rates[0] = fraction(dot_column(o->steering, o, 1));
    o->rotation_rates[2] = (int16_t)(uint16_t)-(uint16_t)fraction(dot_column(o->steering, o, 0));
    o->rotation_rates[1] = 0;
    if (dot_column(o->steering, o, 2) & UINT32_C(0x80000000)) return;
    o->rotation_rates[0] = (int16_t)((int8_t)((uint16_t)o->rotation_rates[0] >> 8) ^ 0x3fff);
    o->rotation_rates[2] = (int16_t)((int8_t)((uint16_t)o->rotation_rates[2] >> 8) ^ 0x3fff);
}

/* The high word of a 32-bit wrapped dot of two columns. */
static int16_t high_dot(const fe2_object *a, unsigned ka, const fe2_object *b, unsigned kb)
{
    uint32_t sum = (uint32_t)((int32_t)a->orientation[ka] * b->orientation[kb]) +
                   (uint32_t)((int32_t)a->orientation[3 + ka] * b->orientation[3 + kb]) +
                   (uint32_t)((int32_t)a->orientation[6 + ka] * b->orientation[6 + kb]);
    return (int16_t)(uint16_t)(sum >> 16);
}

/* A record linked to the reference (its parent) or sharing its parent
 * turns toward the reference's basis; a basis frame (mode set) linked to
 * it turns by half its own terms. Otherwise the rates stay. */
void fe2_motion_rates_toward(fe2_object *o, const fe2_object *reference, int both)
{
    if (o->parent_handle == reference->handle) {
        if (o->station_slot != 0) {
            o->rotation_rates[1] = (int16_t)(uint16_t)-(uint16_t)(int16_t)(o->orientation[3] >> 1);
            if (!both) return;
            o->rotation_rates[0] = (int16_t)(o->orientation[7] >> 1);
            o->rotation_rates[2] = (int16_t)(o->orientation[2] >> 1);
            return;
        }
    } else if (o->parent_handle != reference->parent_handle) {
        return;
    }
    o->rotation_rates[1] = (int16_t)(uint16_t)-(uint16_t)high_dot(o, 0, reference, 1);
    if (!both) return;
    o->rotation_rates[0] = high_dot(o, 1, reference, 2);
    o->rotation_rates[2] = high_dot(o, 2, reference, 0);
}

/* The high word of the 32-bit wrapped dot product of column 0 with the
 * saved direction, shifted down 2 and negated. */
void fe2_motion_radial_rate(const fe2_motion *m, fe2_object *o)
{
    uint32_t sum = (uint32_t)((int32_t)o->orientation[0] * m->radial[0]) +
                   (uint32_t)((int32_t)o->orientation[3] * m->radial[1]) +
                   (uint32_t)((int32_t)o->orientation[6] * m->radial[2]);
    int16_t high = (int16_t)(uint16_t)(sum >> 16);
    o->rotation_rates[1] = (int16_t)(uint16_t)-(uint16_t)(int16_t)(high >> RADIAL_SHIFT);
}
