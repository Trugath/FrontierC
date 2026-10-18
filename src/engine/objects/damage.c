/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* damage, destruction, explosions and debris. */
#include "objects.h"
#include "objects_internal.h"
#include <string.h>

enum {
    MSG_BOUNTY = 0x8635, MSG_RATING = 0x98b8, MSG_CARGO_LOST = 0x98b1, MSG_EQUIPMENT_DAMAGED = 0x98b2,
    MSG_DRIVE_DAMAGED = 0x98b3, MSG_CONTROL_DAMAGED = 0x98b4, RATING_PARAMETER = 0x39,
    PLAYER_MARKER = 0xff, BOUNTY_MARKER = 0xfb
};

/* a debris fragment of `kind` (its name the text 0x8e00 + kind),
 * copied from the source with a random velocity kick. */
fe2_object *fe2_objects_fragment(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *source, uint16_t kind)
{
    fe2_objects_request request;
    fe2_object *o;
    char name[256];
    uint32_t r1, r2, mix;
    int32_t delta[3];
    unsigned i;
    request.flags = 0xf; request.model = 0xb8; request.source = source;
    o = v->allocate(v->context, &request, 1);
    if (!o) return 0;
    o->cargo_type = kind;
    name[0] = 0;
    v->text(v->context, (uint16_t)(kind + 0x8e00u), name, sizeof(name));
    name[sizeof(name) - 1] = 0;
    memcpy(o->name, name, strlen(name) + 1);
    o->map_marker = 0xfa;
    o->role = 0xb;
    o->behaviour = 0x4c;
    o->trail = 6; o->effect = 0;
    r1 = fe2_objects_random(s, NULL);
    r2 = fe2_objects_random(s, &mix);
    delta[0] = (int32_t)r2 >> 22; delta[1] = (int32_t)mix >> 22; delta[2] = (int32_t)r1 >> 22;
    for (i = 0; i < 3; ++i) o->velocity[i] = (int32_t)((uint32_t)o->velocity[i] + (uint32_t)delta[i]);
    v->move(v->context, o, delta);
    return o;
}

static void message(const fe2_objects_services *v, uint16_t selector, uint16_t parameter, uint16_t value,
                    uint32_t amount, unsigned uses)
{
    fe2_objects_message m;
    m.selector = selector; m.parameter = parameter; m.value = value; m.amount = amount; m.uses = uses;
    v->message(v->context, FE2_OBJECTS_MESSAGE_SLOT, &m);
}

/* damage that gets through without destroying: cargo, equipment, a
 * control or the drive. Messages only for the player (marker 0xff). */
static void partial(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *o)
{
    const uint8_t *ext = fe2_objects_extension(v, o);
    int player = o->map_marker == PLAYER_MARKER;
    uint16_t text = 0, selector, r;
    if (player) {
        r = fe2_objects_random_below(s, fe2_objects_word(ext, 8));
        if (r < s->free_capacity) {
            int k;
            if (r >= s->cargo_space) return;
            for (k = FE2_OBJECTS_CARGO_WORDS - 1; k >= 0; --k) {
                r = (uint16_t)(r - s->cargo[k]);
                if (r & 0x8000u) break;
            }
            if (k < 0) return;
            --s->cargo[k];
            ++s->cargo[FE2_CARGO_TOTAL];
            message(v, MSG_CARGO_LOST, 0xffff, (uint16_t)(0x8e00u + (unsigned)k), 0,
                    FE2_OBJECTS_USES_PARAMETER | FE2_OBJECTS_USES_VALUE);
            return;
        }
    }
    r = (uint16_t)fe2_objects_random(s, NULL);
    if (r >= 0x5555) {
        text = fe2_objects_remove_equipment(s, o);
        if (!text) return;
        selector = MSG_EQUIPMENT_DAMAGED;
    } else if (r >= 0x1c71) {
        uint16_t k = fe2_objects_random_below(s, 6);
        ((uint16_t *)o->as.flight.control_limits)[k] = 0;
        if (player) s->lost_controls[k] = 0;
        text = (uint16_t)(k + 0x9997u);
        selector = MSG_CONTROL_DAMAGED;
    } else if (r >= 0x888) {
        /* The drive is damaged: class 1 remains. */
        uint8_t drive = o->as.flight.drive;
        if (drive) {
            o->as.flight.drive = 1;
            if (player) fe2_objects_add_mass(s, (uint16_t)(fe2_objects_preamble_word(2u * drive) - 4u));
        }
        selector = MSG_DRIVE_DAMAGED;
        text = 0;
    } else {
        uint8_t drive = o->as.flight.drive;
        o->as.flight.drive = 0;
        o->fuel = 0;
        if (player) fe2_objects_add_mass(s, fe2_objects_preamble_word(2u * drive));
        selector = MSG_DRIVE_DAMAGED;
        text = 0;
    }
    if (player) message(v, selector, 0xffff, text, 0, FE2_OBJECTS_USES_PARAMETER |
                        (selector == MSG_DRIVE_DAMAGED ? 0u : FE2_OBJECTS_USES_VALUE));
}

/* destruction: the player's bounty and rating, the explosion, hull
 * fragments for ships and cargo canisters, then the release. */
static void destroy(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *o, uint16_t attacker)
{
    fe2_objects_request request;
    fe2_object *explosion;
    uint32_t mix;
    if ((uint8_t)attacker && (uint8_t)attacker == s->player) {
        const uint8_t *ext;
        if (o->map_marker == BOUNTY_MARKER && o->reward && s->player_record &&
            (s->player_record->as.flight.equipment & (UINT32_C(1) << 22))) {
            message(v, MSG_BOUNTY, RATING_PARAMETER, 0, o->reward,
                    FE2_OBJECTS_USES_PARAMETER | FE2_OBJECTS_USES_AMOUNT);
            s->bounty += (uint32_t)10u * (uint16_t)o->reward;
        }
        ext = v->model(v->context, o->model);
        if (fe2_objects_word(ext, 0x1c)) {
            uint32_t before = s->rating;
            ext += fe2_objects_word(ext, 0x1c);
            s->rating = before + (uint32_t)(int32_t)(int16_t)fe2_objects_word(ext, 0x16);
            if ((s->rating >> 16) != (before >> 16)) message(v, MSG_RATING, RATING_PARAMETER, 0, 0, FE2_OBJECTS_USES_PARAMETER);
        }
    }
    v->sound(v->context, 5, o->level);
    request.flags = 9; request.model = 0xbc; request.source = o;
    explosion = v->allocate(v->context, &request, 0);
    if (explosion) {
        const uint8_t *d;
        uint32_t a;
        explosion->role = 0xb;
        explosion->behaviour = 0x64;
        explosion->size_mantissa = 0xffff; explosion->size_exponent = 0xffff;
        explosion->cargo_type = 0xffff;
        a = fe2_objects_random(s, &mix);
        s->effect_seed[0] = (uint16_t)a; s->effect_seed[1] = (uint16_t)mix;
        v->basis(v->context, explosion, s->effect_seed);
        d = v->model(v->context, explosion->model);
        if (fe2_objects_word(d, 0x1c)) {
            uint16_t size = fe2_objects_word(d + fe2_objects_word(d, 0x1c), 6);
            --explosion->model_parameter;
            if (size >= 0xa) {
                v->sound(v->context, 0x1f, explosion->level);
                ++explosion->model_parameter;
                if (size >= 0x190) ++explosion->model_parameter;
            }
        }
        if (o->model >= 0x14 && o->model <= 0x4e) {
            int16_t n = (int16_t)(fe2_objects_random_below(s, 6) - 2u);
            for (; n >= 0; --n) (void)fe2_objects_fragment(s, v, o, 0xf);
            {
                uint16_t count = (uint16_t)(o->capacity >> 1);
                if (count >= 5) count = 4;
                if (count) {
                    n = (int16_t)(fe2_objects_random_below(s, count) - 2u);
                    for (; n >= 0; --n) {
                        unsigned k = (unsigned)fe2_objects_random(s, NULL) & 0x1fu;
                        (void)fe2_objects_fragment(s, v, o, fe2_objects_preamble[0x44 + k]);
                    }
                }
            }
        }
    }
    v->release(v->context, o, attacker);
}

int fe2_objects_damage(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *o, uint16_t amount,
                       uint16_t attacker)
{
    const uint8_t *d;
    uint16_t excess, limit;
    v->damage_callbacks(v->context, o);
    d = v->model(v->context, o->model);
    if ((v->status(v->context, o->handle) & 0x10u) || !fe2_objects_word(d, 0x1c)) {
        v->sound(v->context, 0x1d, o->level);
        return 1;
    }
    /* Shields first, then the hull. */
    if (o->as.flight.shield_charge >= amount) {
        o->as.flight.shield_charge = (uint16_t)(o->as.flight.shield_charge - amount);
        v->sound(v->context, 0x1c, o->level);
        o->frame_state |= 1u;
        goto handled;
    }
    excess = (uint16_t)(amount - o->as.flight.shield_charge);
    o->as.flight.shield_charge = 0;
    limit = o->hull;
    o->hull = (uint16_t)(o->hull - excess);
    if (limit <= excess) { destroy(s, v, o, attacker); return 0; }
    o->trail = 0xa;
    v->sound(v->context, 0x1d, o->level);
    limit = (uint16_t)(o->hull >> 2);
    /* Damage below a quarter of the hull gets through with probability
     * (excess:amount / 2) / limit against a random word. */
    if (excess >= limit) partial(s, v, o);
    else {
        uint32_t dividend = ((uint32_t)excess << 16 | amount) >> 1;
        uint16_t chance = (dividend / limit) > 0xffffu ? (uint16_t)dividend : (uint16_t)(dividend / limit);
        if (chance >= (uint16_t)fe2_objects_random(s, NULL)) partial(s, v, o);
    }
handled:
    if (o->map_marker == PLAYER_MARKER) v->broadcast(v->context, 0x17);
    return 1;
}
