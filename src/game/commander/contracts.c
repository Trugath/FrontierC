/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Passenger, parcel and delivery contracts: acceptance and arrival. */
#include "status_internal.h"

#include <string.h>

enum {
    ARRIVAL_COUNTDOWN = 10,
    CLIENT_NAME_MODE = 3,
    MAX_RELIABILITY = 5,
    ABANDONED_RELIABILITY_PENALTY = 2,
    PAYMENT_UNIT = 10,
    MILITARY_PASSENGER_DELTA = 2,
    LATE_MILITARY_PASSENGER_DELTA = -3,
    PASSENGER_THANKS = 0x9c2e, GROUP_THANKS = 0x9c2f, PARCEL_COLLECTED = 0x9c31,
    DELIVERY_UNPAID = 0x9c32, DELIVERY_DONE = 0x9c30,
    REPORTED_TEXT = 0x9c34, LATE_MILITARY_TEXT = 0x9c35,
    FEDERAL_HQ = 0x9c66, IMPERIAL_HQ = 0x9c65,
    FEDERAL_PORTRAIT = 0x36, IMPERIAL_PORTRAIT = 0x26,
    CHANCE_ROW_MASK = 0x0f,
    SINGLE_SPAWN_MODE = 1, PAIR_SPAWN_MODE = 3, TRIPLE_SPAWN_MODE = 2, MODE_FOUR = 4
};

/* Group size as the arrival code counts cabins. */
static uint16_t cabins_of(const fe2_contract *contract)
{
    return (uint16_t)((contract->group >> 2) + 1);
}

/* Remove contract `index`. Like the original, the record after the last one
 * moves down too. */
static void remove_contract(fe2_commander_status *status, unsigned index)
{
    unsigned count = status->contract_count;
    if (count > FE2_MAX_CONTRACTS) count = FE2_MAX_CONTRACTS;
    if (index < count)
        memmove(&status->contracts[index], &status->contracts[index + 1],
                (count - index) * sizeof status->contracts[0]);
}

static void contract_args(fe2_call_args *args, const fe2_contract *contract)
{
    fe2_args_word(args, PARAM_3, contract->fee);
    fe2_args_word(args, PARAM_4, (uint16_t)(contract->group | contract->kind << 8));
    fe2_args_dword(args, PARAM_1, contract->destination);
    fe2_args_dword(args, PARAM_2, contract->deadline);
    fe2_args_dword(args, PARAM_10, contract->client);
}

static void spawn_contract_objects(const fe2_commander_status *status,
                                   const fe2_game_services *game, fe2_call_args *args,
                                   fe2_contract_spawn spawn)
{
    static const struct { uint16_t mode; unsigned count; } spawns[] = {
        {0, 0}, {SINGLE_SPAWN_MODE, 1}, {PAIR_SPAWN_MODE, 2},
        {TRIPLE_SPAWN_MODE, 3}, {MODE_FOUR, 1}};
    unsigned i;
    for (i = 0; i < spawns[spawn].count; ++i) {
        fe2_args_word(args, PARAM_STYLE, spawns[spawn].mode);
        game->create_object(game->opaque, status->local_authority, args);
    }
}

/* append the contract; its kind may bring company (pirates,
 * assassins...) with the listed chance out of 65536. */
void fe2_status_add_contract(fe2_commander_status *status, const fe2_game_services *game,
                             const fe2_contract *contract)
{
    const fe2_contract_chance *row = &fe2_status_contract_chances[(contract->kind >> 1) &
                                                                CHANCE_ROW_MASK];
    fe2_call_args args;

    if (status->contract_count < FE2_MAX_CONTRACTS)
        status->contracts[status->contract_count] = *contract;
    ++status->contract_count;
    if (row->chance == 0 || (uint16_t)game->random(game->opaque) > row->chance) return;
    fe2_args_clear(&args);
    contract_args(&args, contract);
    fe2_args_word(&args, PARAM_4, (uint16_t)(((contract->kind >> 1) & CHANCE_ROW_MASK) * 4u));
    spawn_contract_objects(status, game, &args, row->spawn);
}

static void report_to_authority(const fe2_game_services *game, fe2_call_args *args,
                                uint16_t headquarters, uint16_t portrait, uint16_t message)
{
    char name[FE2_TEXT_BYTES];
    fe2_args_word(args, PARAM_STYLE, portrait);
    game->format_text(game->opaque, headquarters, args, name);
    fe2_args_subject(args, PARAM_SUBJECT, name);
    game->show_message(game->opaque, message, args);
}

static void client_says(const fe2_game_services *game, fe2_call_args *args, uint32_t client,
                        const char *name, uint16_t message)
{
    fe2_args_word(args, PARAM_STYLE, STYLE_PLAIN);
    fe2_args_subject(args, PARAM_SUBJECT, name);
    fe2_args_dword(args, PARAM_3, client);
    game->show_message(game->opaque, message, args);
}

/* arrival at a contract's destination in time. */
static void deliver(fe2_commander_status *status, const fe2_game_services *game, unsigned index)
{
    fe2_contract contract = status->contracts[index];
    fe2_call_args args;
    char name[FE2_TEXT_BYTES];
    uint16_t message;

    fe2_args_clear(&args);
    contract_args(&args, &contract);
    remove_contract(status, index);
    if ((contract.kind & FE2_CONTRACT_KIND_MASK) == FE2_CONTRACT_GOODS) {
        /* Goods deliveries are never paid on arrival. */
        game->generate_name(game->opaque, CLIENT_NAME_MODE, contract.client, name);
        if (contract.group != FE2_CONTRACT_PARCEL)
            status->cabins_used = (uint16_t)(status->cabins_used - cabins_of(&contract));
        message = DELIVERY_UNPAID;
    } else {
        uint32_t payment = (uint32_t)PAYMENT_UNIT * contract.fee;
        fe2_args_dword(&args, PARAM_3, payment);
        status->cash += payment;
        if (contract.client == 0) {
            /* Military passenger: rank points and possibly a permit. */
            unsigned row = contract.kind & 3u;
            int imperial = (contract.kind & FE2_CONTRACT_IMPERIAL) != 0;
            int8_t permit = fe2_status_rank_adjustments[row + (imperial ? 3u : 0u)];
            fe2_args_word(&args, PARAM_0, MILITARY_PASSENGER_DELTA);
            fe2_args_word(&args, PARAM_4, (uint16_t)(int16_t)permit);
            fe2_status_award(status, game, &args, imperial ? FE2_NAVY_IMPERIAL : FE2_NAVY_FEDERAL,
                          permit, MILITARY_PASSENGER_DELTA);
            fe2_args_word(&args, PARAM_STYLE, imperial ? IMPERIAL_PORTRAIT : FEDERAL_PORTRAIT);
            game->format_text(game->opaque, imperial ? IMPERIAL_HQ : FEDERAL_HQ, &args, name);
            fe2_args_subject(&args, PARAM_SUBJECT, name);
            fe2_args_dword(&args, PARAM_3, contract.client);
            game->show_message(game->opaque, DELIVERY_DONE, &args);
            --status->contract_count;
            fe2_status_contracts_changed(game);
            return;
        }
        if (status->reliability < MAX_RELIABILITY) ++status->reliability;
        game->generate_name(game->opaque, CLIENT_NAME_MODE, contract.client, name);
        if (contract.group == 0) message = PASSENGER_THANKS;
        else if (contract.group == FE2_CONTRACT_PARCEL) message = PARCEL_COLLECTED;
        else message = GROUP_THANKS;
        if (contract.group != FE2_CONTRACT_PARCEL)
            status->cabins_used = (uint16_t)(status->cabins_used - cabins_of(&contract));
    }
    client_says(game, &args, contract.client, name, message);
    --status->contract_count;
    fe2_status_contracts_changed(game);
}

/* an expired contract. A prepaid one is settled at its destination;
 * any other is dropped at once and costs reliability. Military passengers
 * arriving late cost rank; clients report the commander. */
static int settle_expired(fe2_commander_status *status, const fe2_game_services *game,
                          unsigned index)
{
    fe2_contract contract = status->contracts[index];
    fe2_call_args args;
    char name[FE2_TEXT_BYTES];
    uint32_t seed = contract.client;

    fe2_args_clear(&args);
    fe2_args_word(&args, PARAM_4, (uint16_t)(contract.group | contract.kind << 8));
    fe2_args_dword(&args, PARAM_1, contract.client);
    if (contract.kind & FE2_CONTRACT_PREPAID) {
        if (contract.destination != status->current_system) return 0;
        if (contract.client == 0) {
            int imperial = (contract.group & FE2_CONTRACT_GROUP_IMPERIAL) != 0;
            fe2_args_dword(&args, PARAM_0, (uint32_t)LATE_MILITARY_PASSENGER_DELTA);
            fe2_args_dword(&args, PARAM_4, 0xffffffffu);
            fe2_status_award(status, game, &args, imperial ? FE2_NAVY_IMPERIAL : FE2_NAVY_FEDERAL,
                          -1, LATE_MILITARY_PASSENGER_DELTA);
            report_to_authority(game, &args, imperial ? IMPERIAL_HQ : FEDERAL_HQ,
                                imperial ? IMPERIAL_PORTRAIT : FEDERAL_PORTRAIT,
                                LATE_MILITARY_TEXT);
            remove_contract(status, index);
            --status->contract_count;
            fe2_status_contracts_changed(game);
            return 1;
        }
    } else {
        status->reliability = (int16_t)(status->reliability - ABANDONED_RELIABILITY_PENALTY);
    }
    if (contract.group == FE2_CONTRACT_PARCEL) {
        seed = (seed >> 8) | (seed << 24);
        fe2_args_dword(&args, PARAM_1, seed);
    } else {
        /* One fewer cabin than a delivery frees. */
        status->cabins_used = (uint16_t)(status->cabins_used - (int16_t)((contract.group >> 2) - 1));
    }
    game->generate_name(game->opaque, CLIENT_NAME_MODE, seed, name);
    client_says(game, &args, seed, name, REPORTED_TEXT);
    remove_contract(status, index);
    --status->contract_count;
    fe2_status_contracts_changed(game);
    return 1;
}

/* every eleventh call, settle the first contract that has arrived or
 * expired; with none, settle military missions instead. */
void fe2_status_scan_arrivals(fe2_commander_status *status, const fe2_game_services *game)
{
    int count = (int16_t)status->contract_count;
    int index;

    status->arrival_countdown = (uint8_t)(status->arrival_countdown - 1u);
    if ((status->arrival_countdown & 0x80u) == 0) return;
    if (count > 0) {
        status->arrival_countdown = ARRIVAL_COUNTDOWN;
        for (index = 0; index < count && index < FE2_MAX_CONTRACTS; ++index) {
            const fe2_contract *contract = &status->contracts[index];
            if (contract->kind & FE2_CONTRACT_EXPIRED) {
                if (settle_expired(status, game, (unsigned)index)) return;
            } else if (contract->destination == status->current_system) {
                deliver(status, game, (unsigned)index);
                return;
            }
        }
    }
    fe2_status_settle_missions(status, game);
    status->event_flags &= (uint8_t)~1u;
}
