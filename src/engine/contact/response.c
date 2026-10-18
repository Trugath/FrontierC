/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the general motion response. */
#include "contact_internal.h"

enum { RESPONDED = 0x80, OWNED = 1, EVENT_MODEL = 0x10, BOUNCE_BIAS = 10 };

/* A record meeting its parent, or a target of role 1, bounces: its
 * velocity becomes minus a quarter (an arithmetic shift), and a model
 * above 0x10 gets the event with the low word of the bounced magnitudes'
 * sum plus 10. */
static uint8_t bounce(fe2_contact *c, const fe2_contact_services *game, fe2_object *o, fe2_object *t,
                      const int32_t velocity[3])
{
    uint32_t sum = 0;
    unsigned i;
    for (i = 0; i < 3; ++i) {
        uint32_t v = 0u - (uint32_t)(velocity[i] >> 2);
        o->velocity[i] = (int32_t)v;
        sum += fe2_contact_magnitude(v);
    }
    if (o->model > EVENT_MODEL)
        game->collision(game->opaque, o, t, (uint16_t)(sum + BOUNCE_BIAS), 0);
    return c->result;
}

/* Otherwise the velocities exchange 1.5 times their difference (each
 * dword wrapping): the target gains it, then the record loses it. In the
 * response scan, when the target's model is above 0x10 (the original tests
 * the word at the record's segment and the target's offset, the target's
 * in one pool, and then the target's), both get the event with twice the
 * smaller hull and the other's handle, the target first. */
uint8_t fe2_contact_respond(fe2_contact *c, const fe2_contact_services *game, fe2_object *o, fe2_object *t)
{
    int32_t velocity[3];
    uint32_t delta[3];
    uint16_t strength;
    unsigned i;
    c->result |= RESPONDED;
    for (i = 0; i < 3; ++i) velocity[i] = o->velocity[i];
    if (o->parent_handle == t->handle || t->role == OWNED) return bounce(c, game, o, t, velocity);
    for (i = 0; i < 3; ++i) {
        uint32_t d = (uint32_t)velocity[i] - (uint32_t)t->velocity[i];
        d += (uint32_t)((int32_t)d >> 1);
        t->velocity[i] = (int32_t)((uint32_t)t->velocity[i] + d);
        delta[i] = d;
    }
    for (i = 0; i < 3; ++i) o->velocity[i] = (int32_t)((uint32_t)o->velocity[i] - delta[i]);
    if (c->response == 0 || t->model <= EVENT_MODEL) return c->result;
    strength = o->hull;
    if (!(t->hull > strength)) strength = t->hull;
    strength = (uint16_t)(strength << 1);
    game->collision(game->opaque, t, t, strength, o->handle);
    game->collision(game->opaque, o, t, strength, t->handle);
    return c->result;
}
