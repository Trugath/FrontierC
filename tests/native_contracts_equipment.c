/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 *
 * Equipment effects. Kept apart from the other contract checks because the
 * pool and the system records both name a record-size constant.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pool.h"

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "contract failed: %s\n", #cond); abort(); } \
} while (0)

static unsigned sounds;
static uint16_t last_sound;
static uint8_t last_level;
static unsigned damages;
static fe2_object *damaged[8];
static uint16_t damage_kind;
static uint8_t damage_source;

static void play_level(void *opaque, uint16_t effect, uint8_t level)
{
    (void)opaque;
    sounds++;
    last_sound = effect;
    last_level = level;
}

static int16_t near_distance(void *opaque, const fe2_object *from, const fe2_object *to)
{
    (void)opaque;
    (void)from;
    return to->model == 0x4e && to->handle == 2 ? 0x12 : 0x11;
}

static void take_damage(void *opaque, fe2_object *record, uint16_t kind, uint8_t source)
{
    (void)opaque;
    CHECK(damages < 8);
    damaged[damages++] = record;
    damage_kind = kind;
    damage_source = source;
}

static int was_damaged(const fe2_object *object)
{
    unsigned i;
    for (i = 0; i < damages; ++i)
        if (damaged[i] == object) return 1;
    return 0;
}

void equipment_bits_drive_the_effect(void)
{
    fe2_pool_state state;
    fe2_pool_services game;
    fe2_object_pool *pool = calloc(1, sizeof *pool);
    static fe2_object ship, close, distant, heavy, reusable, bystander;
    CHECK(pool != 0);
    memset(&state, 0, sizeof state);
    memset(&game, 0, sizeof game);
    memset(&ship, 0, sizeof ship);
    memset(&close, 0, sizeof close);
    memset(&distant, 0, sizeof distant);
    memset(&heavy, 0, sizeof heavy);
    memset(&reusable, 0, sizeof reusable);
    memset(&bystander, 0, sizeof bystander);
    game.play_level = play_level;
    game.distance = near_distance;
    game.damage = take_damage;
    state.root = pool;
    ship.level = 3;
    ship.handle = 9;
    ship.frame_state = 0xff;
    ship.effect = 9;

    fe2_pool_equipment_effect(&state, &game, &ship);
    CHECK(sounds == 0 && state.game_flags == 0 && ship.effect == 9 && ship.frame_state == 0xff);

    ship.as.flight.equipment = UINT32_C(1) << 19;
    fe2_pool_equipment_effect(&state, &game, &ship);
    CHECK(sounds == 1 && last_sound == 0x19 && last_level == 3);
    CHECK((state.game_flags & FE2_GAME_EFFECT) && !(state.game_flags & FE2_GAME_STRONG_EFFECT));
    CHECK(ship.effect == 2 && ship.frame_state == (uint8_t)((0xffu | 0x20u) & ~0x40u));

    state.game_flags = 0;
    ship.frame_state = 0xff;
    ship.as.flight.equipment |= UINT32_C(1) << 23;
    fe2_pool_equipment_effect(&state, &game, &ship);
    CHECK(sounds == 2 && last_sound == 0x1a);
    CHECK((state.game_flags & FE2_GAME_EFFECT) && (state.game_flags & FE2_GAME_STRONG_EFFECT));

    /* Bit 26 is spent. The player marker grows the player's capacity. */
    ship.as.flight.equipment = (UINT32_C(1) << 26) | UINT32_C(1) << 3;
    ship.map_marker = 0xff;
    ship.capacity = 10;
    state.player_capacity = 10;
    ship.frame_state = 0xff;
    pool->status[0] = 1;
    pool->records[0] = &ship;
    close.model = 0x4e;
    close.handle = 1;
    pool->status[1] = 1;
    pool->records[1] = &close;
    distant.model = 0x4e;
    distant.handle = 2;
    pool->status[2] = 1;
    pool->records[2] = &distant;
    heavy.model = 0x4f;
    heavy.handle = 3;
    pool->status[3] = 1;
    pool->records[3] = &heavy;
    reusable.model = 1;
    reusable.handle = 4;
    pool->status[4] = 0x20;
    pool->records[4] = &reusable;
    bystander.model = 1;
    bystander.handle = 5;
    pool->status[5] = 0;
    pool->records[5] = &bystander;
    damages = 0;
    fe2_pool_nearby_effect(&state, &game, &ship);
    CHECK((ship.as.flight.equipment & (UINT32_C(1) << 26)) == 0);
    CHECK((ship.as.flight.equipment & (UINT32_C(1) << 3)) != 0);
    CHECK(state.player_capacity == 14 && ship.capacity == 10);
    CHECK(ship.effect == 7 && ship.frame_state == (uint8_t)((0xffu | 0x40u) & ~0x20u));
    CHECK(last_sound == 0x1b && last_level == 3);
    CHECK(state.game_flags & FE2_GAME_NEARBY_EFFECT);
    CHECK(was_damaged(&close) && !was_damaged(&distant) && !was_damaged(&heavy));
    CHECK(!was_damaged(&ship) && !was_damaged(&reusable) && !was_damaged(&bystander));
    CHECK(damages == 1 && damage_kind == 0x105 && damage_source == 9);

    /* Another craft's capacity is its own, and a missing bit does nothing. */
    ship.map_marker = 0;
    ship.capacity = 10;
    state.player_capacity = 14;
    ship.as.flight.equipment = UINT32_C(1) << 26;
    damages = 0;
    fe2_pool_nearby_effect(&state, &game, &ship);
    CHECK(ship.capacity == 14 && state.player_capacity == 14 && was_damaged(&close));
    ship.as.flight.equipment = 0;
    ship.capacity = 14;
    damages = 0;
    sounds = 0;
    fe2_pool_nearby_effect(&state, &game, &ship);
    CHECK(ship.capacity == 14 && sounds == 0 && damages == 0);
    free(pool);
}
