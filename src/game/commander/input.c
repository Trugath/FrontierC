/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* keys on the status screen - dismiss crew, select a mining machine,
 * jettison cargo, refuel - and the rotating ship preview. */
#include "status_internal.h"

#include <string.h>

enum {
    LIST_KEY = 0x80,             /* keys 0x80.. pick a list row */
    CREW_KEY_END = 0x94, MINING_KEY_END = 0x8a, CARGO_KEY_END = 0x9a,
    REFUEL_KEY = 0x66,
    CREW_NAME_MODE = 3,
    DISMISSED_TEXT = 0x9c19, MUST_BE_DOCKED_TEXT = 0x9c1a,
    MINING_TARGET_TEXT = 0xa20b,
    JETTISON_TEXT = 0x9c2c, NOT_IN_FLIGHT_TEXT = 0x9c2d,
    FIRST_CARGO_NAME = 0x8e00,
    CANISTER_KIND = 0x0f, CANISTER_MODEL = 0xb8, CANISTER_ROLE = 0x0b, CANISTER_BEHAVIOUR = 0x50,
    JETTISON_EFFECT = 0x20,
    CARGO_EVENT = 0x11,
    MILITARY_DRIVE = 10, TANK_FULL_TEXT = 0x99cc,
    FUEL_UNIT = 0x20000000,
    PREVIEW_SPIN_SHIFT = 3
};

static int docked_or_landed(const fe2_commander_status *status)
{
    return status->flight_state == FE2_FLIGHT_LANDED || status->flight_state == FE2_FLIGHT_DOCKED;
}

static void show_notice(const fe2_game_services *game, uint16_t text)
{
    fe2_call_args args;
    fe2_args_clear(&args);
    fe2_args_word(&args, PARAM_STYLE, STYLE_MESSAGE);
    game->show_message(game->opaque, text, &args);
}

/* Idle frame: spin the preview (not on the mission page). */
static void spin_preview(fe2_commander_status *status, const fe2_game_services *game)
{
    uint32_t step = status->frame_time >> PREVIEW_SPIN_SHIFT;
    if (status->page == FE2_PAGE_CONTRACTS) return;
    fe2_status_render_preview(status, game);
    status->preview_angle[0] = (uint16_t)(status->preview_angle[0] + step);
    status->preview_angle[1] = (uint16_t)(status->preview_angle[1] + (step >> 1));
}

/* Dismiss crew member `index`; only possible on the ground. */
static void dismiss_crew(fe2_commander_status *status, const fe2_game_services *game,
                         unsigned index)
{
    fe2_call_args args;
    char name[FE2_TEXT_BYTES];
    unsigned count = status->crew_count;

    if (!docked_or_landed(status)) {
        show_notice(game, MUST_BE_DOCKED_TEXT);
        return;
    }
    fe2_args_clear(&args);
    fe2_args_word(&args, PARAM_1, (uint16_t)index);
    fe2_args_dword(&args, PARAM_3, status->crew[index].name_seed);
    game->generate_name(game->opaque, CREW_NAME_MODE, status->crew[index].name_seed, name);
    fe2_args_word(&args, PARAM_STYLE, STYLE_PLAIN);
    fe2_args_subject(&args, PARAM_SUBJECT, name);
    game->show_message(game->opaque, DISMISSED_TEXT, &args);
    /* Like the original, the record after the last one moves down too. */
    if (count > FE2_MAX_CREW) count = FE2_MAX_CREW;
    memmove(&status->crew[index], &status->crew[index + 1],
            (count - index) * sizeof status->crew[0]);
    --status->crew_count;
    fe2_status_redraw_crew_page(status, game);
}

/* Make a deployed mining machine in this system the navigation target. */
static void select_mining_unit(fe2_commander_status *status, const fe2_game_services *game,
                               unsigned index)
{
    const fe2_mining_unit *unit = &status->mining[index];
    fe2_call_args args;

    if (unit->system != status->current_system) return;
    status->navigation_target = unit->target;
    fe2_args_clear(&args);
    fe2_args_dword(&args, PARAM_1, unit->system);
    fe2_args_word(&args, PARAM_STYLE, STYLE_MESSAGE);
    game->show_message(game->opaque, MINING_TARGET_TEXT, &args);
}

/* Cargo row `row`: the row-th cargo type held. Returns -1 when none. */
static int cargo_type_at_row(const fe2_commander_status *status, unsigned row)
{
    int type;
    for (type = 0; type < FE2_CARGO_TYPES; ++type) {
        if (status->cargo[type] == 0) {
            if (type >= FE2_CARGO_TYPES - 1) return -1;
            continue;
        }
        if (row-- == 0) return type;
    }
    return -1;
}

/* The canister drifts off along the ship's up axis with a random push. */
static void push_canister(const fe2_commander_status *status, const fe2_game_services *game,
                          fe2_call_args *args, fe2_object *canister)
{
    const fe2_object *ship = status->ship;
    unsigned axis;

    for (axis = 0; axis < 3; ++axis) {
        int32_t along = fe2_status_sar32((int32_t)((uint32_t)(uint16_t)ship->orientation[3 * axis + 1]
                                                << 16), 1);
        int32_t jitter = fe2_status_sar32((int32_t)game->random(game->opaque), 1);
        fe2_args_dword(args, PARAM_2 + axis, (uint32_t)along + (uint32_t)jitter);
    }
    for (axis = 0; axis < 3; ++axis) {
        int32_t push = fe2_status_sar32((int32_t)args->arg[PARAM_2 + axis].value, 0x17);
        fe2_args_dword(args, PARAM_2 + axis, (uint32_t)push);
        canister->velocity[axis] = (int32_t)((uint32_t)canister->velocity[axis] + (uint32_t)push);
    }
}

/* The ship recoils against the local gravity. */
static void recoil(const fe2_commander_status *status, const fe2_game_services *game,
                   fe2_call_args *args)
{
    const fe2_object *ship = status->ship;
    int16_t gravity = (int16_t)(0u - ship->model_scale_b[0]);   /* the gravity word */
    unsigned axis;

    for (axis = 0; axis < 3; ++axis) {
        int32_t product = (int32_t)ship->orientation[3 * axis + 1] * gravity;
        int16_t scaled = (int16_t)(uint16_t)((uint32_t)product >> 15);
        fe2_args_dword(args, PARAM_0 + axis, (uint32_t)(int32_t)scaled << 1);
    }
    game->apply_impulse(game->opaque, args);
    game->play_effect(game->opaque, JETTISON_EFFECT);
}

static void jettison(fe2_commander_status *status, const fe2_game_services *game, unsigned row)
{
    fe2_call_args args;
    fe2_object *canister;
    int type = cargo_type_at_row(status, row);
    uint8_t subtype;

    if (type < 0) {
        spin_preview(status, game);
        return;
    }
    if (status->flight_state != FE2_FLIGHT_MANUAL) {
        show_notice(game, NOT_IN_FLIGHT_TEXT);
        return;
    }
    fe2_args_clear(&args);
    fe2_args_word(&args, PARAM_2, status->cargo[type]);
    --status->cargo[type];
    --status->cargo_used;
    subtype = fe2_status_cargo_subtypes[type];
    fe2_args_word(&args, PARAM_1, (uint16_t)(FIRST_CARGO_NAME + type));
    fe2_args_word(&args, PARAM_STYLE, STYLE_MESSAGE);
    game->show_message(game->opaque, JETTISON_TEXT, &args);

    fe2_args_objects(&args, PARAM_OBJECTS, status->objects);
    fe2_args_object(&args, PARAM_SUBJECT, status->ship);
    fe2_args_word(&args, PARAM_0, CANISTER_KIND);
    fe2_args_word(&args, PARAM_1, CANISTER_MODEL);
    canister = game->spawn_object(game->opaque, &args);
    if (canister != 0) {
        char name[FE2_TEXT_BYTES];
        unsigned i;
        canister->cargo_type = subtype;
        game->format_text(game->opaque, (uint16_t)(FIRST_CARGO_NAME + subtype), &args, name);
        for (i = 0; i < sizeof canister->name; ++i)
            if ((canister->name[i] = name[i]) == 0) break;
        canister->role = CANISTER_ROLE;
        canister->behaviour = CANISTER_BEHAVIOUR;
        push_canister(status, game, &args, canister);
        recoil(status, game, &args);
    }
    fe2_status_redraw_cargo_page(status, game);
    fe2_args_byte(&args, PARAM_2, status->local_authority);
    game->post_event(game->opaque, CARGO_EVENT, &args);
}

/* burn a tonne of fuel cargo into the tank. Military drives (class 10
 * and above) burn military fuel and leave a tonne of radioactives; hydrogen
 * frees its cargo space. */
static void refuel(fe2_commander_status *status, const fe2_game_services *game)
{
    fe2_object *ship = status->ship;
    uint16_t capacity;

    uint16_t *cargo = status->cargo;

    if (ship->as.craft.drive >= MILITARY_DRIVE) {
        if (cargo[FE2_CARGO_MILITARY_FUEL] == 0) {
            spin_preview(status, game);
            return;
        }
        --cargo[FE2_CARGO_MILITARY_FUEL];
        ++cargo[FE2_CARGO_RADIOACTIVES];
    } else {
        if (cargo[FE2_CARGO_HYDROGEN_FUEL] == 0) {
            spin_preview(status, game);
            return;
        }
        --cargo[FE2_CARGO_HYDROGEN_FUEL];
        --status->cargo_used;
    }
    status->ship_flags &= (uint8_t)~1u;
    game->ship_type(game->opaque, ship->model);
    /* The saved limits (0x2133) occupy the same object bytes an empty tank
     * clears. Putting them back is what makes the engines answer again. */
    memcpy(ship->as.flight.control_limits, status->registration, sizeof status->registration);
    capacity = (uint16_t)(fe2_status_fuel_capacity[ship->as.craft.drive] << 8);
    ship->fuel += FUEL_UNIT;
    if (capacity <= (uint16_t)(ship->fuel >> 16)) {
        ship->fuel = (uint32_t)capacity << 16;
        show_notice(game, TANK_FULL_TEXT);
    }
    fe2_status_redraw_cargo_page(status, game);
}

void fe2_status_input(fe2_commander_status *status, const fe2_game_services *game)
{
    uint8_t key;

    if (!game->poll_key(game->opaque, &key)) {
        spin_preview(status, game);
        return;
    }
    if (key >= LIST_KEY) {
        unsigned row = (unsigned)(key - LIST_KEY);
        if (status->page == FE2_PAGE_CREW) {
            if (key < CREW_KEY_END) {
                if ((int)row < (int16_t)status->crew_count) dismiss_crew(status, game, row);
                else spin_preview(status, game);
                return;
            }
        } else if (status->page == FE2_PAGE_MINING) {
            if (key < MINING_KEY_END) {
                if ((int)row < (int16_t)status->mining_count)
                    select_mining_unit(status, game, row);
                spin_preview(status, game);
                return;
            }
        } else if (key < CARGO_KEY_END) {
            jettison(status, game, row);
            return;
        }
    }
    if (key == REFUEL_KEY) {
        refuel(status, game);
        return;
    }
    game->forward_key(game->opaque);
    spin_preview(status, game);
}
