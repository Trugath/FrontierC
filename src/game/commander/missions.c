/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Military missions: placing targets on arrival and settling on return. */
#include "status_internal.h"

#include <string.h>

enum {
    KILL_TARGET_MODE = 6,           /* people creation mode for an escorted target */
    RECON_TARGET_KIND = 0x0e,       /* spawn kind for film/bomb targets */
    RECON_TARGET_ROLE = 0x0e,
    RECON_TARGET_FLAGS = 0x60,
    PLACED_MARK = 0xff,
    TARGET_DISTANCE_BITS = 0x37,
    PAYMENT_UNIT = 10,
    FAILED_TEXT = 0x9c26, SUCCESS_TEXT = 0x9c27, TARGET_LOST_TEXT = 0x9c28,
    LATE_TEXT = 0x9c29, FILM_OK_TEXT = 0x9c2a, FILM_EXCELLENT_TEXT = 0x9c2b,
    FILM_EXCELLENT_QUALITY = 10,
    FEDERAL_AUTHORITY = 0x36,
    EXCELLENT_FILM_POINTS = 0x14
};


void fe2_status_match_target(fe2_commander_status *status, const fe2_military_contract *mission,
                             const fe2_object *object)
{
    if (strncmp(object->name, mission->target_name, sizeof mission->target_name) == 0)
        status->matched_handle = object->handle;
}

void fe2_status_target_visitor(void *context, const fe2_object *object)
{
    fe2_target_search *search = context;
    fe2_status_match_target(search->status, search->mission, object);
}

static uint8_t find_target(fe2_commander_status *status, const fe2_game_services *game,
                           const fe2_military_contract *mission)
{
    fe2_target_search search;
    search.status = status;
    search.mission = mission;
    status->matched_handle = 0;
    game->visit_objects(game->opaque, status->objects, fe2_status_target_visitor, &search);
    return status->matched_handle;
}

/* Arithmetic shift of the 64-bit value high:0 right by `count`. */
static void place_axis(uint32_t high, unsigned count, uint32_t *out_high, uint32_t *out_low)
{
    uint32_t low = 0;
    for (; count > 0; --count) {
        low = (low >> 1) | (high << 31);
        high = (uint32_t)fe2_status_sar32((int32_t)high, 1);
    }
    *out_high = high;
    *out_low = low;
}

/* Kill missions 0..2: before the deadline, an escort appears around the
 * target once it is found among the system's objects. */
static void place_escort(fe2_commander_status *status, const fe2_game_services *game,
                         fe2_military_contract *mission)
{
    fe2_call_args args;
    uint8_t handle;

    fe2_args_clear(&args);
    fe2_args_dword(&args, PARAM_1, status->time_of_day);
    fe2_args_dword(&args, PARAM_2, status->day);
    if (status->day > mission->deadline ||
        (status->day == mission->deadline && status->time_of_day > mission->target_seed))
        return;
    handle = find_target(status, game, mission);
    if (handle == 0) return;
    fe2_args_dword(&args, PARAM_5, mission->target_seed);
    fe2_args_dword(&args, PARAM_3, mission->target_seed);
    fe2_args_dword(&args, PARAM_4, mission->deadline);
    fe2_args_byte(&args, PARAM_2, handle);
    fe2_args_word(&args, PARAM_STYLE, KILL_TARGET_MODE);
    game->create_object(game->opaque, (uint16_t)(mission->target_model | mission->quality << 8),
                        &args);
}

/* Film and bomb missions 3..6: the target appears beside the object found
 * by name, at a distance set by that object's model size. */
static void place_recon_target(fe2_commander_status *status, const fe2_game_services *game,
                               fe2_military_contract *mission, unsigned type)
{
    fe2_call_args args;
    fe2_object *target, *anchor;
    const fe2_ship_type *model;
    uint32_t vector[3];
    uint8_t handle;
    unsigned axis, shift;
    int16_t distance;

    handle = find_target(status, game, mission);
    if (handle == 0) return;
    fe2_args_clear(&args);
    fe2_args_word(&args, PARAM_1, fe2_status_contract_text[(mission->type >> 4) & 7u]);
    fe2_args_word(&args, PARAM_0, RECON_TARGET_KIND);
    fe2_args_template(&args, PARAM_SUBJECT, fe2_status_object_template);
    target = game->spawn_object(game->opaque, &args);
    if (target == 0) return;
    target->name[0] = (type & 1u) ? '*' : '+';
    target->name[1] = 0;
    target->as.craft.equipment_damaged &= 0xffffu;
    target->flags |= RECON_TARGET_FLAGS;
    target->role = RECON_TARGET_ROLE;
    target->parent_handle = handle;

    anchor = game->lookup_object(game->opaque, handle);
    status->view_angle[0] = (uint16_t)mission->target_seed;
    target->target_seed = (mission->target_seed >> 16) | (mission->target_seed << 16);
    status->view_angle[1] = (uint16_t)(mission->target_seed >> 16);
    target->station_slot = PLACED_MARK;
    game->orient_object(game->opaque, target);
    for (axis = 0; axis < 3; ++axis)
        vector[axis] = (uint32_t)(uint16_t)target->orientation[3 * axis + 1] << 16;

    model = game->ship_type(game->opaque, anchor->model);
    fe2_args_dword(&args, PARAM_6, (uint32_t)model->scale << 16);
    game->square_vector(game->opaque, vector);
    distance = (int16_t)(TARGET_DISTANCE_BITS - model->radius[0] - model->radius[1]);
    shift = distance > 0 ? (unsigned)distance + 1u : 1u;
    for (axis = 0; axis < 3; ++axis)
        place_axis(vector[axis], shift, &target->position_high[axis], &target->position_low[axis]);
    mission->target_model = target->handle;
    mission->quality = PLACED_MARK;
}

/* Event 2: entering a mission's target system. */
void fe2_status_place_mission_targets(fe2_commander_status *status, const fe2_game_services *game)
{
    int remaining = (int16_t)status->military_count;
    fe2_military_contract *mission = status->military;

    for (; remaining > 0 && mission < status->military + FE2_MAX_MILITARY_CONTRACTS;
         --remaining, ++mission) {
        unsigned type = mission->type & 0x0f;
        if (status->current_system != mission->target_system) continue;
        if (type <= 2) place_escort(status, game, mission);
        else if (type <= 6) place_recon_target(status, game, mission, type);
    }
}

typedef struct mission_result {
    uint16_t message;      /* zero: not settled yet */
    int16_t rank_points;
    int elite_set;         /* the elite points change is part of this result */
    int32_t elite_points;  /* applied for mission type 0 only */
    int paid;
} mission_result;

/* Outcome of mission `type` (0..6) on returning to its home system. */
static mission_result judge(const fe2_commander_status *status,
                            const fe2_military_contract *mission, unsigned type,
                            fe2_call_args *args)
{
    static const mission_result pending = {0, 0, 0, 0, 0};
    static const mission_result failed = {FAILED_TEXT, -5, 1, -0x400, 0};
    static const mission_result lost = {TARGET_LOST_TEXT, -1, 1, -0x200, 0};
    static const mission_result killed = {SUCCESS_TEXT, 0x0c, 1, 0x200, 1};
    static const mission_result late = {LATE_TEXT, -1, 1, -0x400, 0};
    static const mission_result film_ok = {FILM_OK_TEXT, 0x10, 0, 0, 1};
    static const mission_result film_excellent = {FILM_EXCELLENT_TEXT, 0x14, 0, 0, 1};
    static const mission_result bombed = {SUCCESS_TEXT, 0x12, 0, 0, 1};

    if (type <= 2) {
        if (mission->outcome > 0) return lost;
        if (mission->outcome < 0) return killed;
        fe2_args_dword(args, PARAM_1, status->time_of_day);
        fe2_args_dword(args, PARAM_2, status->day);
        if (status->day < mission->deadline ||
            (status->day == mission->deadline && status->time_of_day < mission->target_seed))
            return pending;
        return failed;
    }
    fe2_args_dword(args, PARAM_2, status->day);
    if (status->day > mission->deadline) return mission->outcome == 0 ? failed : late;
    if (mission->outcome == 0) return pending;
    if (type > 4) return bombed;
    return mission->quality > FILM_EXCELLENT_QUALITY ? film_ok : film_excellent;
}

/* Remove mission `index` of `count`. */
static void remove_mission(fe2_commander_status *status, unsigned index, unsigned count)
{
    if (count > FE2_MAX_MILITARY_CONTRACTS) count = FE2_MAX_MILITARY_CONTRACTS;
    if (index + 1 < count)
        memmove(&status->military[index], &status->military[index + 1],
                (count - index - 1) * sizeof status->military[0]);
}

/* Elite points: losses stop at zero, gains wrap. */
static void add_elite_points(fe2_commander_status *status, int32_t delta)
{
    uint32_t magnitude = delta < 0 ? 0u - (uint32_t)delta : (uint32_t)delta;
    if (delta >= 0) status->elite_points += magnitude;
    else status->elite_points = status->elite_points >= magnitude
                                ? status->elite_points - magnitude : 0;
}

/* Settle one mission; the record is removed before the debriefing. Failed
 * missions always cost imperial rank: the original takes the authority byte
 * from the negative points, which never equals the federal authority. */
static void debrief(fe2_commander_status *status, const fe2_game_services *game,
                    unsigned index, unsigned type, fe2_call_args *args, mission_result result)
{
    const fe2_military_contract mission = status->military[index];
    char name[FE2_TEXT_BYTES];
    fe2_call_args briefing;
    uint8_t authority;
    int16_t permit = -1;

    if (result.paid) {
        uint32_t payment = (uint32_t)PAYMENT_UNIT * mission.reward;
        fe2_args_dword(args, PARAM_3, payment);
        status->cash += payment;
    }
    if (result.elite_set) fe2_args_dword(args, PARAM_6, (uint32_t)result.elite_points);
    fe2_args_word(args, PARAM_5, (uint16_t)result.rank_points);
    fe2_args_word(args, PARAM_1, result.message);
    remove_mission(status, index, status->military_count);
    --status->military_count;

    briefing = *args;
    game->format_text(game->opaque, fe2_status_mission_text[type], &briefing, name);
    fe2_args_subject(&briefing, PARAM_SUBJECT, name);
    fe2_args_word(&briefing, PARAM_STYLE, fe2_status_mission_authority[type]);
    game->show_message(game->opaque, result.message, &briefing);

    if (type == 0) {
        add_elite_points(status, (int32_t)args->arg[PARAM_6].value);
    } else {
        fe2_args_word(args, PARAM_4, 0xffff);
        fe2_args_word(args, PARAM_0, (uint16_t)result.rank_points);
        authority = (uint8_t)result.rank_points;
        if (result.rank_points >= 0) {
            authority = fe2_status_mission_authority[type];
            permit = fe2_status_mission_permit[type];
            if (result.rank_points == EXCELLENT_FILM_POINTS) ++permit;
            fe2_args_word(args, PARAM_4, (uint16_t)permit);
        }
        fe2_status_award(status, game, args,
                      authority == FEDERAL_AUTHORITY ? FE2_NAVY_FEDERAL : FE2_NAVY_IMPERIAL,
                      permit, result.rank_points);
    }
    fe2_status_contracts_changed(game);
}

/* missions whose home system is the current one. Mission types above
 * 6 do not occur; the original would read its code as table data for them. */
void fe2_status_settle_missions(fe2_commander_status *status, const fe2_game_services *game)
{
    fe2_call_args args;
    int remaining = (int16_t)status->military_count;
    unsigned index = 0;

    fe2_args_clear(&args);
    for (; remaining > 0 && index < FE2_MAX_MILITARY_CONTRACTS; --remaining) {
        const fe2_military_contract *mission = &status->military[index];
        unsigned type = mission->type & 0x0f;
        mission_result result;

        if (mission->home_system != status->current_system || type > 6) {
            ++index;
            continue;
        }
        fe2_args_word(&args, PARAM_STYLE, (uint16_t)type);
        result = judge(status, mission, type, &args);
        if (result.message == 0) {
            ++index;
            continue;
        }
        debrief(status, game, index, type, &args, result);
    }
}
