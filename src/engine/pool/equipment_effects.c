/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* equipment effects. */
#include "pool.h"

enum {
    STRONG_EQUIPMENT = 23, EFFECT_EQUIPMENT = 19, NEARBY_EQUIPMENT = 26,
    EFFECT_SHOWN = 2, EFFECT_NEARBY = 7, FRAME_EFFECT = 0x20, FRAME_NEARBY = 0x40,
    SOUND_STRONG = 0x1a, SOUND_EFFECT = 0x19, SOUND_NEARBY = 0x1b,
    PLAYER = 0xff, CAPACITY_STEP = 4, LAST_AFFECTED_MODEL = 0x4e, NEARBY_DISTANCE = 0x11, NEARBY_DAMAGE = 0x105
};

static void show(fe2_object *o, uint8_t effect, uint8_t on, uint8_t off)
{
    o->effect = effect;
    o->frame_state = (uint8_t)((o->frame_state | on) & ~off);
}

/* Equipment bit 23 takes priority over bit 19. */
void fe2_pool_equipment_effect(fe2_pool_state *s, const fe2_pool_services *game, fe2_object *o)
{
    uint32_t equipment = o->as.flight.equipment;
    if (equipment & (UINT32_C(1) << STRONG_EQUIPMENT)) {
        s->game_flags |= FE2_GAME_EFFECT | FE2_GAME_STRONG_EFFECT;
        show(o, EFFECT_SHOWN, FRAME_EFFECT, FRAME_NEARBY);
        game->play_level(game->opaque, SOUND_STRONG, o->level);
    } else if (equipment & (UINT32_C(1) << EFFECT_EQUIPMENT)) {
        s->game_flags |= FE2_GAME_EFFECT;
        show(o, EFFECT_SHOWN, FRAME_EFFECT, FRAME_NEARBY);
        game->play_level(game->opaque, SOUND_EFFECT, o->level);
    }
}

typedef struct nearby {
    const fe2_pool_services *game;
    fe2_object *source;
} nearby;

/* Small models near the source take damage from it. */
static void affect(void *context, fe2_object *candidate, uint16_t status_pair)
{
    nearby *n = (nearby *)context;
    (void)status_pair;
    if (candidate->model > LAST_AFFECTED_MODEL || candidate == n->source) return;
    if (n->game->distance(n->game->opaque, n->source, candidate) > NEARBY_DISTANCE) return;
    n->game->damage(n->game->opaque, candidate, NEARBY_DAMAGE, n->source->handle);
}

/* Equipment bit 26 is used up: capacity grows, the effect shows and the
 * records around (in the root pool) are affected. */
void fe2_pool_nearby_effect(fe2_pool_state *s, const fe2_pool_services *game, fe2_object *o)
{
    nearby n;
    uint32_t bit = UINT32_C(1) << NEARBY_EQUIPMENT;
    if (!(o->as.flight.equipment & bit)) return;
    o->as.flight.equipment &= ~bit;
    if (o->map_marker == PLAYER) s->player_capacity = (uint16_t)(s->player_capacity + CAPACITY_STEP);
    else o->capacity = (uint16_t)(o->capacity + CAPACITY_STEP);
    show(o, EFFECT_NEARBY, FRAME_NEARBY, FRAME_EFFECT);
    game->play_level(game->opaque, SOUND_NEARBY, o->level);
    n.game = game;
    n.source = o;
    fe2_pool_visit(s, s->root, affect, &n);
    s->game_flags |= FE2_GAME_NEARBY_EFFECT;
}
