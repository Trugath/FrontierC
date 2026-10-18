/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* game events - arrivals, deadlines, wages and redraws. */
#include "status_internal.h"

enum {
    CONTRACTS_CHANGED_EVENT = 0x19,
    ARRIVAL_COUNTDOWN = 10,
    DEADLINE_SHIP_FLAGS = 0xf3,
    LATE_RELIABILITY_PENALTY = 2,
    CLIENT_NAME_MODE = 3,
    ABANDONED_TEXT = 0x9c33
};

/* tell the game the contract list changed. */
void fe2_status_contracts_changed(const fe2_game_services *game)
{
    fe2_call_args args;
    fe2_args_clear(&args);
    game->post_event(game->opaque, CONTRACTS_CHANGED_EVENT, &args);
}

static int docked_or_landed(const fe2_commander_status *status)
{
    return status->flight_state == FE2_FLIGHT_LANDED || status->flight_state == FE2_FLIGHT_DOCKED;
}

static void schedule_arrival_scan(fe2_commander_status *status)
{
    status->event_flags |= 1u;
    status->arrival_countdown = ARRIVAL_COUNTDOWN;
}

/* the first contract to pass its deadline is marked expired. A paying
 * passenger left waiting complains and costs reliability. */
void fe2_status_check_deadlines(fe2_commander_status *status, const fe2_game_services *game)
{
    int remaining = (int16_t)status->contract_count;
    fe2_contract *contract = status->contracts;

    if (status->flight_state == FE2_FLIGHT_HYPERSPACE) return;
    status->ship_flags &= DEADLINE_SHIP_FLAGS;
    for (; remaining > 0; --remaining, ++contract) {
        fe2_call_args args;
        char name[FE2_TEXT_BYTES];

        if (status->day <= contract->deadline || (contract->kind & FE2_CONTRACT_EXPIRED)) continue;
        contract->kind |= FE2_CONTRACT_EXPIRED;
        if (contract->client == 0) return;
        status->reliability = (int16_t)(status->reliability - LATE_RELIABILITY_PENALTY);
        if (contract->kind & FE2_CONTRACT_PREPAID) return;
        fe2_args_clear(&args);
        fe2_args_dword(&args, PARAM_1, contract->client);
        game->generate_name(game->opaque, CLIENT_NAME_MODE, contract->client, name);
        fe2_args_word(&args, PARAM_STYLE, STYLE_PLAIN);
        fe2_args_subject(&args, PARAM_SUBJECT, name);
        fe2_args_dword(&args, PARAM_3, contract->client);
        game->show_message(game->opaque, ABANDONED_TEXT, &args);
        if (docked_or_landed(status)) schedule_arrival_scan(status);
        return;
    }
    fe2_status_contracts_changed(game);
}

/* pay the crew's weekly salaries. A shortfall empties the account and
 * is added to the wages owed. Owed wages are then settled from any cash; if
 * the cash cannot cover them, the original clears both cash and debt. */
static int pay_crew(fe2_commander_status *status, const fe2_game_services *game)
{
    uint32_t total = 0, balance;
    unsigned i, count = status->crew_count;

    if (count == 0) return FE2_STATUS_OK;
    for (i = 0; i < count && i < FE2_MAX_CREW; ++i)
        total += (uint32_t)(int32_t)status->crew[i].salary;
    balance = status->cash - total;
    if (status->cash < total) {
        status->unpaid_wages -= balance;
        balance = 0;
    }
    status->cash = balance;
    if (status->unpaid_wages != 0 && status->cash != 0) {
        if (status->cash >= status->unpaid_wages) status->cash -= status->unpaid_wages;
        else status->cash = 0;
        status->unpaid_wages = 0;
    }
    if (status->screen_active) return fe2_status_select_page(status, game, status->page);
    return FE2_STATUS_OK;
}

int fe2_status_event(fe2_commander_status *status, const fe2_game_services *game,
                     uint16_t event)
{
    switch (event) {
    case FE2_EVENT_SYSTEM_ENTERED:
        if (status->contract_count != 0 || status->military_count != 0)
            schedule_arrival_scan(status);
        return FE2_STATUS_OK;
    case FE2_EVENT_NEW_DAY:
        fe2_status_check_deadlines(status, game);
        return FE2_STATUS_OK;
    case FE2_EVENT_PAY_CREW:
        return pay_crew(status, game);
    case FE2_EVENT_REDRAW_A:
    case FE2_EVENT_REDRAW_B:
    case FE2_EVENT_REDRAW_C:
        if (status->screen_active) return fe2_status_select_page(status, game, status->page);
        return FE2_STATUS_OK;
    case FE2_EVENT_MILITARY_ARRIVAL:
        fe2_status_place_mission_targets(status, game);
        return FE2_STATUS_OK;
    default:
        return FE2_STATUS_OK;
    }
}
