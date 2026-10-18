/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Timers, the per-pass ship countdowns and the contact countdown
 *. */
#include "flight_internal.h"

void fe2_flight_timers(fe2_flight_state *state, const fe2_flight_services *game, fe2_object *record,
                       uint32_t interval)
{
    flight_context c;
    fe2_object *ship, *other;
    uint16_t step = (uint16_t)interval, scaled;
    c.s = state;
    c.g = game;
    c.subject = 0;
    /* Both timers saturate at zero (16-bit, the heat's step << 7 wraps). */
    state->delay_ticks = state->delay_ticks < step ? 0 : (uint16_t)(state->delay_ticks - step);
    if (state->heat != 0) {
        scaled = (uint16_t)(step << 7);
        state->heat = state->heat < scaled ? 0 : (uint16_t)(state->heat - scaled);
    }
    game->advance_failures(game->opaque, record, interval);
    if (state->mode != FE2_FLIGHT_AUTOPILOT) {
        game->update_capacity(game->opaque, record);
        return;
    }
    other = game->people_update(game->opaque, record, interval);
    ship = state->ship;
    if (ship->behaviour == 0x74) {
        /* The autopilot reached its target: fly relative to it. */
        state->reference = other;
        state->reference_handle = other->handle;
        if (other->flags & 0x40) {
            state->mode = FE2_FLIGHT_ENTER_DOCKED;
            game->refresh_contacts(game->opaque, record);
            flight_refresh_mode_display(&c, 0);
            return;
        }
        state->control_flags &= (uint8_t)~FE2_CONTROL_ANNOTATE;
        if (other->flags & 0x20) {
            state->mode = FE2_FLIGHT_REFERENCE_ALIGNED;
            state->measured = 0;
        } else {
            state->mode = FE2_FLIGHT_REFERENCE_OFFSET;
        }
        flight_refresh_mode_display(&c, 0);
        return;
    }
    if (other->flags & 0x10) return;
    if (ship->behaviour_parent != ship->parent_handle) return;
    if ((int16_t)ship->steering_shift > 0x17) return;
    state->mode = FE2_FLIGHT_MANUAL;
    flight_message(&c, TEXT_AUTOPILOT_OFF, 0);
    state->throttle = 0;
    flight_refresh_mode_display(&c, 0);
}

/*
 * 0xead. The cooldown (+0xbc) saturates at zero; frame steps above 0xffff
 * clear it. In autopilot mode the mode-four control flag follows
 * behaviour <= 0x6c. The cargo word +0x74 then falls by half the step under
 * that flag, or rises by it (saturating at 0xffff, with a speed warning
 * outside autopilot once the speed reaches 100000).
 *
 * The step is the original's EAX, which the mode display may replace
 * when the flag changes (see flight_refresh_mode_display).
 */
void flight_ship_countdowns(flight_context *c, fe2_object *ship)
{
    fe2_flight_state *s = c->s;
    uint32_t eax = s->frame_step;
    int32_t level;
    if (ship->cooldown != 0) {
        if (eax > 0xffff || ship->cooldown < (uint16_t)eax) ship->cooldown = 0;
        else ship->cooldown = (uint16_t)(ship->cooldown - (uint16_t)eax);
    }
    if (s->mode == FE2_FLIGHT_AUTOPILOT) {
        s->motion_flags &= (uint8_t)~0x02;
        if (ship->behaviour > 0x6c) {
            if (s->control_flags & FE2_CONTROL_MODE_FOUR) {
                s->control_flags &= (uint8_t)~FE2_CONTROL_MODE_FOUR;
                eax = flight_refresh_mode_display(c, eax);
            }
        } else if (!(s->control_flags & FE2_CONTROL_MODE_FOUR)) {
            s->control_flags |= FE2_CONTROL_MODE_FOUR;
            eax = flight_refresh_mode_display(c, eax);
        }
    }
    eax >>= 1;
    if (s->control_flags & FE2_CONTROL_MODE_FOUR) {
        level = (int32_t)((uint32_t)ship->cargo_type - eax);
        ship->cargo_type = level < 0 ? 0 : (uint16_t)level;
        return;
    }
    if (s->mode != FE2_FLIGHT_AUTOPILOT && (int32_t)s->speed >= 100000 && !(s->motion_flags & 0x02)) {
        s->motion_flags |= 0x02;
        flight_message(c, TEXT_SPEED_WARNING, 0);
    }
    level = (int32_t)((uint32_t)ship->cargo_type + eax);
    ship->cargo_type = level > 0xffff ? 0xffff : (uint16_t)level;
}

/* 0x96. The byte countdown runs outside autopilot; at 1 it ends with a
 * message (and goes to 0xff), and at 0xf9 it reports the reference to offences. */
void fe2_flight_countdown(fe2_flight_state *state, const fe2_flight_services *game)
{
    flight_context c;
    fe2_call_args args;
    int8_t old = state->countdown;
    c.s = state;
    c.g = game;
    c.subject = 0;
    if (old == 0 || state->mode == FE2_FLIGHT_AUTOPILOT) return;
    state->countdown = (int8_t)(uint8_t)(old - 1);
    if (old > 1) return;
    if (old == 1) {
        state->countdown = (int8_t)(uint8_t)(state->countdown - 1);
        flight_object_message(&c, state->reference, TEXT_COUNTDOWN_END);
        return;
    }
    if ((uint8_t)state->countdown != 0xf8) return;
    fe2_args_clear(&args);
    fe2_args_object(&args, P_OBJECT, state->reference);
    game->offences_event(game->opaque, 0, &args);
}
