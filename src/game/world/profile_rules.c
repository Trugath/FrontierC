/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* profile rules. Run after a profile record is finalized,
 * they refine the body's class and temperature from the system's danger
 * level and may add stations. Physical meanings of the classes (record
 * models and kinds) remain unresolved; values below are the original's. */
#include "systems_internal.h"

enum {
    NAME_STATION = 6, NAME_SOL_STATION = 7, NAME_INHABITED = 8, NAME_OUTPOST = 9,
    NAME_COLONY = 0x0a
};

typedef struct rule_body {
    fe2_system_generation *g;
    unsigned *record;
    uint16_t danger;
} rule_body;

static fe2_system_record *body(const rule_body *b) { return fe2_systems_record(b->g, *b->record); }
static uint16_t temperature(const rule_body *b)
{
    return fe2_record_word(body(b), FE2_RECORD_TEMPERATURE);
}
static void set_temperature(const rule_body *b, uint16_t value)
{
    fe2_record_set_word(body(b), FE2_RECORD_TEMPERATURE, value);
}
static void set_class(const rule_body *b, uint16_t model, uint16_t kind)
{
    fe2_record_set_word(body(b), FE2_RECORD_DESCRIPTOR, model);
    fe2_record_set_word(body(b), FE2_RECORD_KIND, kind);
}
static void name(const rule_body *b, uint16_t type) { fe2_systems_generate_name(b->g, *b->record, type); }
static void stations(const rule_body *b, uint16_t scale, uint16_t base)
{
    fe2_systems_append_stations(b->g, b->record, scale, base);
}
static void sol_station(const rule_body *b, uint16_t choice)
{
    fe2_systems_append_sol_station(b->g, b->record, choice);
}

static rule_body rule_body_of(fe2_system_generation *g, unsigned *record)
{
    rule_body b;
    b.g = g;
    b.record = record;
    b.danger = g->danger;
    return b;
}

/* stations around an inhabited world, by danger level. */
static void inhabited_stations(const rule_body *b, uint16_t base)
{
    uint16_t cx = b->danger;
    stations(b, (uint16_t)(cx >> 5), base);
    if ((int16_t)cx < 0x1e) return;
    sol_station(b, (int16_t)cx < 0x32 ? 0 : (int16_t)cx < 0xf2 ? 1 : 2);
}

/* a temperate inhabited world. `reference` is the temperature value
 * the caller compares (not always the stored temperature). */
static void temperate_world(const rule_body *b, uint16_t reference)
{
    if ((int16_t)reference < 0x127) set_temperature(b, 0x127);
    set_class(b, 0x8a, 0x16);
    name(b, NAME_INHABITED);
    inhabited_stations(b, 0);
}

static void rule_warm_and_settle_body(fe2_system_generation *g, unsigned *record);


static void rule_reclassify_temperature_and_settlement(fe2_system_generation *g, unsigned *record)
{
    rule_body b = rule_body_of(g, record);
    uint16_t cx = b.danger, t = temperature(&b), margin;
    uint8_t chance;

    if (t > 0x139) {
        set_class(&b, 0x84, 0x18);
        set_temperature(&b, (uint16_t)(t + ((uint16_t)g->value >> 1)));
        return;
    }
    if (t <= 0xd7) {
        if ((int16_t)t < 0xaa) {
            if ((int16_t)t > 0x7b || (int16_t)t < 0x42) {
                rule_warm_and_settle_body(g, record);
                return;
            }
            set_temperature(&b, (uint16_t)(t + 0x16));
            set_class(&b, 0x86, 0x0e);
            return;
        }
        t = (uint16_t)(t + 0x19);
        set_temperature(&b, t);
        if ((int16_t)cx >= 0xc8) temperate_world(&b, t);
        else set_class(&b, 0x84, 0x0c);
        return;
    }
    set_temperature(&b, (uint16_t)(t + 0x23));
    chance = (uint8_t)((uint8_t)fe2_systems_random(g) >> 2);
    margin = (uint16_t)(temperature(&b) - 0xfa);
    if (chance > (uint8_t)margin) {
        if ((int16_t)cx < 0x78) set_class(&b, 0x8e, 0x10);
        else temperate_world(&b, margin);
        return;
    }
    if ((int16_t)cx < 4) return;
    name(&b, NAME_OUTPOST);
    if ((int16_t)cx < 0x10) return;
    if ((int16_t)temperature(&b) <= 0x118) {
        fe2_record_set_word(body(&b), FE2_RECORD_DESCRIPTOR, 0x8c);
        stations(&b, (uint16_t)(cx >> 6), 0x30);
        if ((int16_t)cx < 0x28) return;
        sol_station(&b, (int16_t)cx < 0x50 ? 0 : 1);
        return;
    }
    if ((g->random_a & 0x0fu) == 0) {
        fe2_record_set_word(body(&b), FE2_RECORD_DESCRIPTOR, 0x8e);
        inhabited_stations(&b, 0x48);
    } else {
        inhabited_stations(&b, 0);
    }
}


static void rule_warm_and_settle_body(fe2_system_generation *g, unsigned *record)
{
    rule_body b = rule_body_of(g, record);
    uint16_t cx = b.danger, t = (uint16_t)(temperature(&b) + 0x14);

    set_temperature(&b, t);
    if ((int16_t)cx >= 0x96 && (int16_t)t >= 0x111 && (int16_t)t <= 0x139) {
        set_temperature(&b, 0x127);
        set_class(&b, 0x88, 0x12);
        name(&b, NAME_INHABITED);
        stations(&b, (uint16_t)(cx >> 6), 0);
        sol_station(&b, (int16_t)cx < 0xb4 ? 0 : 1);
        return;
    }
    set_class(&b, 0x7e, 0x08);
    if ((int16_t)t > 0x212) return;
    if (!((int16_t)cx >= 0xc8 && (int16_t)t >= 0xa0)) {
        if ((int16_t)t < 0xb4 || (int16_t)cx <= 6) return;
    }
    if ((int16_t)fe2_record_word(body(&b), FE2_RECORD_SIZE_EXPONENT) >= 0x47)
        fe2_record_set_word(body(&b), FE2_RECORD_SIZE_EXPONENT, 0x47);
    name(&b, NAME_COLONY);
    if ((int16_t)cx < 0x1e) return;
    stations(&b, 0, 0x18);
    if ((int16_t)cx < 0x64) return;
    sol_station(&b, (int16_t)cx < 0xaa ? 0 : 1);
}


static void rule_settle_moderate_temperature_body(fe2_system_generation *g, unsigned *record)
{
    rule_body b = rule_body_of(g, record);
    int16_t cx = (int16_t)b.danger, t = (int16_t)temperature(&b);

    if (t > 0x1f4 || t < 0x96) return;
    if (cx <= 0xb4 && (t < 0xc8 || cx <= 8)) return;
    name(&b, NAME_COLONY);
    if (cx < 0x2d) return;
    stations(&b, 0, 0x18);
    if (cx < 0x96) return;
    sol_station(&b, 0);
}


static void rule_settle_warm_body(fe2_system_generation *g, unsigned *record)
{
    rule_body b = rule_body_of(g, record);
    int16_t cx = (int16_t)b.danger;

    if ((int16_t)temperature(&b) < 0xdc || cx <= 0x32) return;
    name(&b, NAME_COLONY);
    if (cx < 0x50) return;
    stations(&b, 0, 0x18);
}

/* zero size; a warm enough body in a busy system is named. */
static void rule_clear_size(fe2_system_generation *g, unsigned *record)
{
    rule_body b = rule_body_of(g, record);
    fe2_record_set_word(body(&b), FE2_RECORD_SIZE_MANTISSA, 0);
    fe2_record_set_word(body(&b), FE2_RECORD_SIZE_EXPONENT, 0x38);
    if ((int16_t)temperature(&b) < 0xdc || (int16_t)b.danger <= 0x32) return;
    name(&b, NAME_COLONY);
}

/* beyond the second record, rewrite as class 0xae. */
static void rule_promote(fe2_system_generation *g, unsigned *record)
{
    fe2_system_record *r = fe2_systems_record(g, *record);
    if ((int16_t)g->record_count <= 2) return;
    fe2_record_set_word(r, FE2_RECORD_DESCRIPTOR, 0xae);
    fe2_record_set_word(r, FE2_RECORD_KIND, 0x32);
    fe2_record_set_dword(r, FE2_RECORD_PROFILE_OR_OBJECT_REFERENCE, 994);
    fe2_record_set_word(r, FE2_RECORD_TEMPERATURE, 0x2c09);
    fe2_record_set_word(r, FE2_RECORD_SIZE_EXPONENT,
                        (uint16_t)(fe2_record_word(r, FE2_RECORD_SIZE_EXPONENT) - 1));
}

/* promote when the next random state A is negative. */
static void rule_promote_after_negative(fe2_system_generation *g, unsigned *record)
{
    if (fe2_systems_random(g) & 0x80000000u) rule_promote(g, record);
}

/* two negative samples in a row; a first nonnegative one stops. */
static void rule_promote_after_two_negatives(fe2_system_generation *g, unsigned *record)
{
    if ((fe2_systems_random(g) & 0x80000000u) == 0) return;
    rule_promote_after_negative(g, record);
}

static const fe2_profile_rule rules[FE2_PROFILES] = {
    0, rule_clear_size, rule_settle_warm_body, rule_settle_moderate_temperature_body, rule_warm_and_settle_body, rule_reclassify_temperature_and_settlement, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    rule_promote, rule_promote_after_two_negatives, rule_promote_after_negative,
    rule_promote_after_negative, 0, 0, 0, 0
};

fe2_profile_rule fe2_systems_profile_rule(unsigned profile)
{
    return profile < FE2_PROFILES ? rules[profile] : 0;
}
