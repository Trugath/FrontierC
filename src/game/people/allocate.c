/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Allocation and the new craft's equipment.
 *
 * The original works the budget in the argument block: parameter 0 holds
 * the roll, 4 (a word) the capacity left and 5 the equipment bits, and
 * leaves them there. */
#include "people_internal.h"
#include "game/model.h"

enum {
    FLIGHT_MOUNTS = 0x12,              /* flight parameter word: weapon mounts */
    INSTALLED_AT = 4,                  /* : installed[4] onwards */
    RANGE_CAP = 0x12c,
    FUEL_CAP = 0xaf
};

static uint16_t people_budget(const people_context *c)
{
    return (uint16_t)people_arg(c, P4);
}

static void people_spend(people_context *c, uint16_t amount)
{
    fe2_args_word(c->args, P4, (uint16_t)(people_budget(c) - amount));
}

static void people_fit(people_context *c, unsigned bit)
{
    fe2_args_dword(c->args, P5, people_arg(c, P5) | (uint32_t)1 << bit);
}

/* the cargo bay class from a roll scaled by the budget. */
static uint8_t people_capacity_class(people_context *c, uint8_t marker)
{
    uint8_t roll = (uint8_t)(people_random_below(c, 0x33) + people_table_byte(T_CAPACITY_ROLL + marker));
    uint16_t share = (uint16_t)(((uint32_t)roll << 8) * people_budget(c) >> 16);
    unsigned band = 0;

    do
        band += 2;
    while (share > people_table_word(T_EQUIPMENT_BAND + band));
    people_spend(c, people_table_word(T_EQUIPMENT_COST + band));
    return people_table_byte(T_EQUIPMENT + band / 2);
}

static void people_equip(people_context *c, fe2_object *object, uint8_t marker, const uint8_t *flight, uint16_t roll)
{
    int16_t drives = (int16_t)fe2_model_word(flight, FE2_FLIGHT_DRIVE);
    uint16_t amount;
    uint8_t drive;

    fe2_args_word(c->args, P0, roll);
    fe2_args_word(c->args, P4, object->capacity);
    fe2_args_dword(c->args, P5, 0);

    /* The drive: the low byte of the model's pair, the high one for good
     * rolls (a better drive costs one more and fits bit 8). */
    if (drives >= 0) {
        drive = (uint8_t)drives;
        if (marker == 8 || roll >= 0xc000) {
            if ((marker == 8 || roll >= 0xf000) && marker != 1) {
                people_spend(c, 1);
                people_fit(c, 8);
            }
            drive = (uint8_t)((uint16_t)drives >> 8);
        }
        object->as.flight.drive = drive;
        people_spend(c, people_table_word(T_DRIVE_MASS + 2u * drive));
    }
    drive = object->as.flight.drive;
    object->fuel = (uint32_t)people_table_byte(T_DRIVE_CLASS + drive) << 24;
    amount = people_table_byte(T_DRIVE_FUEL + drive);
    if (people_budget(c) < amount)
        amount = people_budget(c);
    people_spend(c, amount);
    object->drive_fuel = (uint8_t)amount;
    object->as.flight.installed[0] = people_capacity_class(c, marker);

    /* Range: two nested rolls over the budget (signed cap). */
    amount = people_budget(c);
    if ((int16_t)amount > RANGE_CAP)
        amount = RANGE_CAP;
    if (marker == 1)
        amount >>= 2;
    amount >>= 2;
    amount = (uint16_t)((uint32_t)amount * roll >> 16);
    amount = people_random_below(c, people_random_below(c, amount));
    amount = (uint16_t)(amount << 2);
    people_spend(c, amount);
    object->as.flight.shield_capacity = (uint16_t)(amount << 4);

    if (people_budget(c) >= 2) {
        uint16_t pick = people_random_below(c, roll);

        if (pick >= 0xc000) {
            people_fit(c, 7);
            people_spend(c, 2);
        } else if (pick >= 0x4000) {
            people_fit(c, 3);
            people_spend(c, 2);
        }
    }
    if (people_budget(c) == 0)
        goto done;

    /* Weapons in the mounts from the last chosen one down, while the
     * budget lasts (running out skips the rest). */
    {
        int16_t mount = (int16_t)(people_random_below(c, (uint16_t)(fe2_model_word(flight, FLIGHT_MOUNTS) + 1)) - 1);

        if (mount >= 0) {
            uint8_t weapon = people_table_byte(T_INSTALLED + (roll >> 12));

            do {
                object->as.flight.installed[INSTALLED_AT + mount] = weapon;
                people_spend(c, 1);
                if (people_budget(c) == 0)
                    goto fuel;
            } while (--mount >= 0);
        }
    }
    if (people_random_below(c, roll) >= 0x800) {
        people_spend(c, 1);
        people_fit(c, 2);
    }
    if (roll >= 0xfc00 && people_budget(c) >= 4 && marker != MARKER_STATION_SEED) {
        people_spend(c, 4);
        people_fit(c, 10);
    }
fuel:
    people_fit(c, 14);
    amount = (uint16_t)((uint32_t)people_table_word(T_FUEL_SHARE + 2u * marker) * people_budget(c) >> 16);
    if (amount >= FUEL_CAP)
        amount = FUEL_CAP;
    people_spend(c, amount);
    object->drive_fuel = (uint8_t)(object->drive_fuel + amount);
done:
    object->capacity = people_budget(c);
    object->as.flight.equipment = people_arg(c, P5) >> 16 | people_arg(c, P5) << 16;
}

/* allocate (parameter 0 kind 0x4f, 1 the model, 8 the template) and
 * equip the craft. */
static int people_allocate_common(people_context *c, uint8_t marker)
{
    fe2_object *object = people_object_arg(c, P_CREATED);
    const uint8_t *model;
    const uint8_t *flight;
    uint16_t roll;
    int made;

    fe2_args_word(c->args, P0, 0x4f);
    made = c->g->allocate_upper(c->g->opaque, c->args, &object);
    people_set_object(c, P_CREATED, object);
    if (!made)
        return 0;
    object->role = FE2_PEOPLE_CRAFT;
    object->map_marker = marker;
    people_generate_identity(c, object, people_table_byte(T_NAME_TYPE + marker));
    model = c->g->model(c->g->opaque, object->model);
    flight = model + fe2_model_word(model, FE2_MODEL_FLIGHT);
    people_forget(c, P_CALLBACK);         /* the flight block's address */
    roll = (uint16_t)people_random(c, 0);
    if (object->map_marker == MARKER_STATION_SEED) {
        object->target_seed = 0;
        roll = (uint16_t)(c->s->system_seed << 8);
    }
    if (object->map_marker == MARKER_CONTRACT)
        roll = 0xffff;
    people_equip(c, object, marker, flight, roll);
    return 1;
}

/* market chooses the model; the template is the module's. */
int people_allocate(people_context *c, uint8_t marker)
{
    fe2_args_word(c->args, P1, people_market(c, 8, marker).result);
    fe2_args_template(c->args, P_CURRENT, fe2_people_template);
    return people_allocate_common(c, marker);
}

/* `model`, from the caller's template (parameter 8). */
int people_allocate_model(people_context *c, uint8_t marker, uint16_t model)
{
    fe2_args_word(c->args, P1, model);
    return people_allocate_common(c, marker);
}
