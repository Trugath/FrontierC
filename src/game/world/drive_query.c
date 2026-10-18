/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the drive query - range, fuel, jump time and payment. */
#include "hyperspace_internal.h"

enum {
    FUEL_DIVISOR = 20001, MILITARY_CLASSES = 10, TIME_FACTOR = 7, MINIMUM_RANGE = 300,
    FIRST_RANGED_CLASS = 2
};

/* Unsigned 32/16 division with a 16-bit quotient; fails where DIV faults. */
static int divide(uint32_t dividend, uint16_t divisor, uint16_t *quotient)
{
    if (divisor == 0 || dividend / divisor > 0xffffu) return 0;
    *quotient = (uint16_t)(dividend / divisor);
    return 1;
}

static uint32_t widen(uint16_t value) { return (uint32_t)(int32_t)(int16_t)value; }

static void refuse(fe2_drive_query *q)
{
    q->range_left = 0;
    q->mode = 0xffff;
}

/* An account pays the fuel and reports the range left: 20001 * fuel / hull
 * mass, capped at the drive's range. */
static int pay_from_account(fe2_hyperspace_state *s, const fe2_object *ship, fe2_drive_query *q,
                            uint16_t hull_mass)
{
    uint16_t fuel_needed = (uint16_t)q->fuel_needed, left;
    int military = ship->as.craft.drive >= MILITARY_CLASSES;
    uint16_t *fuel = military ? &s->military_fuel : &s->hydrogen_fuel;
    q->range_left = *fuel;
    if (fuel_needed > *fuel) {
        refuse(q);
        return FE2_HYPERSPACE_OK;
    }
    if (q->mode == FE2_DRIVE_CONSUME) {
        *fuel = (uint16_t)(*fuel - fuel_needed);
        if (military) s->radioactives = (uint16_t)(s->radioactives + fuel_needed);
        else s->cargo_used = (uint16_t)(s->cargo_used - fuel_needed);
        q->range_left = (uint16_t)(q->range_left - fuel_needed);
    }
    if (!divide((uint32_t)FUEL_DIVISOR * q->range_left, hull_mass, &left))
        return FE2_HYPERSPACE_DIVIDE_FAULT;
    q->range_left = left < (uint16_t)q->range ? left : (uint16_t)q->range;
    return FE2_HYPERSPACE_OK;
}

int fe2_hyperspace_query(fe2_hyperspace_state *s, const fe2_hyperspace_services *game,
                         fe2_object *ship, fe2_drive_query *q)
{
    fe2_ship_stats stats = game->ship_stats(game->opaque, ship->model);
    const fe2_drive_class *drive = &s->drive_classes[ship->as.craft.drive];
    uint16_t range, fuel_needed, ratio;
    q->name_text = drive->name_text;
    q->mass = drive->mass;
    if (!divide(drive->range_factor, stats.hull_mass, &range)) return FE2_HYPERSPACE_DIVIDE_FAULT;
    q->range = widen(range);
    if (q->range < q->request || q->request == 0) {
        q->fuel_needed = (q->fuel_needed & 0xffff0000u) | 0xffffu;
        refuse(q);
        return FE2_HYPERSPACE_OK;
    }
    /* Fuel: hull mass * distance / 20001 + 1 (16-bit). */
    if (!divide((uint32_t)stats.hull_mass * (uint16_t)q->request, FUEL_DIVISOR, &fuel_needed))
        return FE2_HYPERSPACE_DIVIDE_FAULT;
    fuel_needed = (uint16_t)(fuel_needed + 1);
    q->fuel_needed = widen(fuel_needed);
    /* Jump time: the distance's low word as the high word of a dword whose
     * low word is its sign, less one, over the range, times 7. */
    if (!divide(((uint32_t)(uint16_t)q->request << 16 | (uint16_t)(widen((uint16_t)q->request) >> 16)) - 1u,
                range, &ratio))
        return FE2_HYPERSPACE_DIVIDE_FAULT;
    q->request = (uint32_t)ratio * TIME_FACTOR;
    if (ship->handle == s->player_handle) return pay_from_account(s, ship, q, stats.hull_mass);
    /* Other craft compare and pay only the low byte. */
    if ((uint8_t)fuel_needed > ship->drive_fuel) refuse(q);
    else if (q->mode == FE2_DRIVE_CONSUME) ship->drive_fuel = (uint8_t)(ship->drive_fuel - (uint8_t)fuel_needed);
    return FE2_HYPERSPACE_OK;
}

int fe2_hyperspace_class_ranges(const fe2_hyperspace_state *s, const fe2_hyperspace_services *game,
                                uint16_t model, uint32_t ranges[FE2_RANGED_CLASSES])
{
    fe2_ship_stats stats = game->ship_stats(game->opaque, model);
    unsigned i;
    for (i = 0; i < FE2_RANGED_CLASSES; ++i) {
        const fe2_drive_class *drive = &s->drive_classes[FIRST_RANGED_CLASS + i];
        uint16_t range = 0;
        if (drive->mass < stats.drive_limit) {
            if (!divide(drive->range_factor, stats.hull_mass, &range)) return FE2_HYPERSPACE_DIVIDE_FAULT;
            if (range < MINIMUM_RANGE) range = 0;
        }
        ranges[i] = widen(range);
    }
    return FE2_HYPERSPACE_OK;
}
