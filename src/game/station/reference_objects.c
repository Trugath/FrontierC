/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* reference objects - stations and their docking slots.
 * Craft are attached to a slot, cleared to dock, released and given the
 * slot's position relative to the station; the player gets messages. */
#include "market_internal.h"

enum {
    REQUEST_FIND_MARKET, REQUEST_ATTACH, REQUEST_DETACH, REQUEST_ACTIVATE, REQUEST_CLEAR,
    REQUEST_CLAIM, REQUEST_DOCK, REQUEST_MATCH, REQUEST_CATEGORY
};

enum {
    ROTATING_FLAG = 0x40, FIXED_FLAG = 0x20,
    STATE_IDLE = 0, STATE_RELEASED = 1, STATE_ACTIVE = 3, STATE_CLAIMED = 0xff,
    DOCKING_TIME = 0xe10,
    FRAME_SHIFT = 6, CONTROL_SHIFT = 2,
    MIN_FINE_BLOCK = 10000,
    TEXT_REFUSED = 0x98a2, TEXT_BUSY = 0x98a0, TEXT_NO_SLOT = 0x98a1,
    TEXT_CLEARED = 0x98a3, TEXT_CLEARED_AGAIN = 0x98a5,
    DOCKING_MUSIC = 6, MESSAGE_PORTRAIT = 0x18
};

/* Signed 32-bit values with their sign extensions, as published in the
 * draft (low dwords lookup/system/due, high dwords reward/detail/kind). */
static void publish(fe2_market_draft *d, uint32_t x, uint32_t y, uint32_t z)
{
    d->lookup = x;
    d->system = y;
    d->due = z;
    d->reward = (x & 0x80000000u) ? 0xffffffffu : 0;
    d->detail = (y & 0x80000000u) ? 0xffffffffu : 0;
    d->kind = (z & 0x80000000u) ? 0xffffffffu : 0;
}

static uint32_t arithmetic_shift(uint32_t value, unsigned count)
{
    while (count-- > 0) value = (value >> 1) | (value & 0x80000000u);
    return value;
}

/* the slot vector (three words at `base` + slot * 8 bytes) turned
 * into the station's frame: each orientation row dotted with it, doubled,
 * then shifted down six bits. */
static void frame_vector(fe2_market_state *s, const fe2_object *station, const uint16_t *table,
                         unsigned table_words, uint16_t base)
{
    uint16_t slot = (uint16_t)s->draft.threshold;
    unsigned at = (uint16_t)(base + (uint16_t)(slot << 3)) / 2u;
    uint32_t row[3];
    unsigned k;

    fe2_market_set_low(&s->draft.counter, (uint16_t)(slot << 3));
    if (at + 2 >= table_words) return;     /* past the native tables */
    fe2_market_set_low(&s->draft.lookup, table[at]);
    for (k = 0; k < 3; ++k) {
        uint32_t sum = (uint32_t)((int32_t)station->orientation[3 * k] * (int16_t)table[at]) +
                       (uint32_t)((int32_t)station->orientation[3 * k + 1] * (int16_t)table[at + 1]) +
                       (uint32_t)((int32_t)station->orientation[3 * k + 2] * (int16_t)table[at + 2]);
        row[k] = arithmetic_shift(sum << 1, FRAME_SHIFT);
    }
    publish(&s->draft, row[0], row[1], row[2]);
}

static int is_player(const fe2_market_state *s, uint8_t handle) { return handle == s->player; }

/* attach a craft to a docking slot of the target station. A slot
 * taken by this craft is reused; otherwise the highest free one is
 * adopted. draft.counter reports adoption. */
void fe2_market_attach(fe2_market_state *s, fe2_reference_call *call)
{
    fe2_object *station = call->target;
    uint8_t handle = (uint8_t)call->ax;
    uint16_t adopted = 0;
    int16_t slot;

    for (slot = (int16_t)station->as.body.shape; slot >= 0; --slot) {
        uint8_t *docked = &station->as.body.docked[slot & 7];
        if (*docked == handle) break;
        if (*docked == 0) {
            *docked = handle;
            adopted = 0xffff;
            break;
        }
    }
    if (slot >= 0) {
        if (is_player(s, handle)) s->player_slot = (uint8_t)slot;
        fe2_market_set_low(&s->draft.threshold, (uint16_t)slot);
        if (!(station->flags & ROTATING_FLAG)) {
            publish(&s->draft, 0, 0, 0xffffffffu);
        } else {
            uint16_t base = station->as.body.shape == 3 ? 0x0e : station->as.body.shape == 1 ? 0 : 0x2c;
            frame_vector(s, station, fe2_market_attachment_vectors,
                         FE2_MARKET_ATTACHMENT_VECTORS, base);
        }
        call->ax = 0;
    }
    fe2_market_set_low(&s->draft.counter, adopted);
}

/* claim the station's docking clearance for the craft. */
void fe2_market_claim(fe2_reference_call *call)
{
    fe2_body_fields *b = &call->target->as.body;
    uint8_t handle = (uint8_t)call->ax;
    if (b->docking_state == STATE_IDLE || b->docking_state == STATE_RELEASED ||
        handle == b->docking_claim) {
        b->docking_state = STATE_CLAIMED;
        b->docking_claim = handle;
        b->docking_timer = DOCKING_TIME;
        call->ax = (uint16_t)((call->ax & 0xff00u) | 1u);
    } else {
        call->ax = 0;
    }
}

/* a docking message to the player about the target station. */
void fe2_market_player_message(fe2_market_state *s, const fe2_market_services *game,
                               uint8_t handle, uint16_t text, fe2_object *target)
{
    fe2_call_args args;
    if (!is_player(s, handle)) return;
    if (text == TEXT_CLEARED && s->music_enabled && s->current_music != DOCKING_MUSIC)
        game->select_music(game->opaque, DOCKING_MUSIC);
    fe2_args_clear(&args);
    fe2_args_dword(&args, 3, (target->target_seed >> 16) | (target->target_seed << 16));
    fe2_args_object(&args, 8, target);
    fe2_args_dword(&args, 2, (uint16_t)(s->draft.threshold + 1u));
    fe2_args_word(&args, 7, MESSAGE_PORTRAIT);
    game->show_message(game->opaque, text, &args);
}

static void release(fe2_object *station)
{
    station->as.body.docking_state = STATE_IDLE;
    station->as.body.docking_claim = 0;
    station->as.body.docking_timer = 0;
}

/* a craft asks the station to dock. The player is refused while
 * fined 10000 or more, or when flag 0x201b is clear. */
static void request_docking(fe2_market_state *s, const fe2_market_services *game,
                            fe2_reference_call *call)
{
    uint8_t handle = call->actor->handle;
    call->ax = (uint16_t)((call->ax & 0xff00u) | handle);
    if (is_player(s, handle) && (s->local_offence_fine >= MIN_FINE_BLOCK || s->event_base == 0)) {
        fe2_market_player_message(s, game, handle, TEXT_REFUSED, call->target);
        call->ax = 1;
        return;
    }
    fe2_market_claim(call);
    if (call->ax == 0) {
        fe2_market_player_message(s, game, call->actor->handle, TEXT_BUSY, call->target);
        call->ax = 0xffff;
        return;
    }
    call->ax = (uint16_t)((call->ax & 0xff00u) | call->actor->handle);
    fe2_market_attach(s, call);
    if (call->ax != 0) {
        release(call->target);
        fe2_market_player_message(s, game, call->actor->handle, TEXT_NO_SLOT, call->target);
        call->ax = 1;
        return;
    }
    fe2_market_player_message(s, game, call->actor->handle,
                              (uint16_t)s->draft.counter != 0 ? TEXT_CLEARED : TEXT_CLEARED_AGAIN,
                              call->target);
    call->ax = 0;
}

/* control vector of the current slot (draft.threshold). */
static void slot_vector(fe2_market_state *s, fe2_reference_call *call)
{
    const fe2_object *station = call->target;
    uint16_t saved = (uint16_t)s->draft.counter;
    uint16_t shape = station->as.body.shape;

    if (station->flags & ROTATING_FLAG) {
        uint16_t base = shape == 3 ? 0x1e : shape == 1 ? 0 : shape != 0 ? 0x3c : 0x5a;
        frame_vector(s, station, fe2_market_command_vectors, FE2_MARKET_COMMAND_VECTORS, base);
    } else if (!(station->flags & FIXED_FLAG)) {
        unsigned at = (uint16_t)((uint16_t)s->draft.threshold << 3) / 2u;
        if (at + 2 < FE2_MARKET_CONTROL_VECTORS) {
            const uint16_t *v = &fe2_market_control_vectors[at];
            publish(&s->draft, (uint32_t)(int32_t)(int16_t)v[0], (uint32_t)(int32_t)(int16_t)v[1],
                    (uint32_t)(int32_t)(int16_t)v[2] << CONTROL_SHIFT);
        }
    } else {
        frame_vector(s, station, fe2_market_control_vectors, FE2_MARKET_CONTROL_VECTORS, 0x1e);
    }
    call->ax = 0;
    fe2_market_set_low(&s->draft.counter, saved);
}

void fe2_market_reference(fe2_market_state *s, const fe2_market_services *game,
                          fe2_reference_call *call)
{
    fe2_body_fields *b = call->target ? &call->target->as.body : 0;
    uint8_t handle = (uint8_t)call->ax;

    switch (call->request) {
    case REQUEST_FIND_MARKET: {
        /* Checks one record past the counted markets. */
        uint16_t index;
        for (index = 0; index <= s->market_count; ++index)
            if (fe2_market_byte(s, fe2_market_market_at(index) + FE2_MARKET_OBJECT) == handle) break;
        call->market = index;
        return;
    }
    case REQUEST_ATTACH:
        fe2_market_attach(s, call);
        return;
    case REQUEST_DETACH: {
        int16_t slot;
        for (slot = (int16_t)b->shape; slot >= 0; --slot)
            if (b->docked[slot & 7] == handle) {
                b->docked[slot & 7] = 0;
                break;
            }
        b->docking_state = STATE_RELEASED;
        b->docking_claim = 0;
        b->docking_timer = 0;
        return;
    }
    case REQUEST_ACTIVATE:
        if (handle == b->docking_claim) {
            b->docking_state = STATE_ACTIVE;
            b->docking_timer = DOCKING_TIME;
            call->ax = 1;
        } else {
            call->ax = 0;
        }
        return;
    case REQUEST_CLEAR:
        release(call->target);
        return;
    case REQUEST_CLAIM:
        fe2_market_claim(call);
        return;
    case REQUEST_DOCK:
        request_docking(s, game, call);
        return;
    case REQUEST_MATCH:
        call->ax = handle == b->docking_claim ? 0xffff : 0;
        return;
    case REQUEST_CATEGORY: {
        fe2_market_draft saved = s->draft;
        fe2_market_set_low(&s->draft.kind, call->ax);
        call->ax = fe2_market_category_value(s, game);
        s->draft.due = saved.due;
        s->draft.reward = saved.reward;
        s->draft.detail = saved.detail;
        s->draft.kind = saved.kind;
        s->draft.seed = saved.seed;
        return;
    }
    default:
        slot_vector(s, call);
        return;
    }
}
