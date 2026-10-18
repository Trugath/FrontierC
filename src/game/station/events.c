/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Events: landing fee, berthing fee and refresh (0x76 / 0x7a3..0x8da). */
#include "station_internal.h"

uint32_t station_landing_charge(int16_t seed)
{
    if (seed < STATION_FEE_TIER_LO) return 10;
    if (seed < STATION_FEE_TIER_MID) return 20;
    if (seed < STATION_FEE_TIER_HI) return 30;
    return 40;
}

uint32_t station_berthing_charge(int16_t seed)
{
    if (seed < STATION_FEE_TIER_LO) return 5;
    if (seed < STATION_FEE_TIER_MID) return 10;
    if (seed < STATION_FEE_TIER_HI) return 20;
    return 30;
}

/* Paid from the balance, or added to the fines owed (the landing fee
 * undoes an addition that turns the total negative). */
static void apply_landing_fee(station_context *c)
{
    fe2_station_state *s = c->s;
    uint32_t charge = station_landing_charge(s->system_seed);
    uint16_t text = 0x993d;

    s->event_word = 0;
    station_reset_selection_keys(c);
    station_set(c, P_SUBJECT, s->station);
    station_set(c, P3, charge);
    if (s->balance >= charge) {
        s->balance -= charge;
    } else {
        s->deferred += charge;
        if ((int32_t)s->deferred < 0) s->deferred -= charge;
        text = 0x993e;
    }
    station_set_word(c, P_STYLE, 0x18);
    station_show_message(c, text);
}

static void apply_berthing_fee(station_context *c)
{
    fe2_station_state *s = c->s;
    uint32_t charge;
    uint16_t text = 0x9940;

    if (s->flight_state != FE2_STATION_MODE_BERTH_A && s->flight_state != FE2_STATION_MODE_BERTH_B) return;
    s->event_word = 0;
    if (s->deferred != 0) {
        station_process_deferred(c);
        if (c->stop) return;
    }
    charge = station_berthing_charge(s->system_seed);
    station_set(c, P3, charge);
    if (s->balance >= charge) {
        s->balance -= charge;
    } else {
        s->deferred += charge;
        text = 0x993f;
    }
    if (s->screen_active != 0) station_dispatch_action(c);
    station_set(c, P3, charge);
    station_set(c, P_SUBJECT, s->station);
    station_set_word(c, P_STYLE, 0x18);
    station_show_message(c, text);
}

void fe2_station_event(fe2_station_state *state, const fe2_station_services *game, fe2_call_args *args,
                       uint16_t selector)
{
    station_context c = {state, game, args, 0};
    switch (selector) {
    case FE2_STATION_EVENT_LANDING: apply_landing_fee(&c); break;
    case FE2_STATION_EVENT_BERTHING: apply_berthing_fee(&c); break;
    case FE2_STATION_EVENT_REFRESH:
        if (state->screen_active != 0) station_dispatch_action(&c);
        break;
    default: break;
    }
}
