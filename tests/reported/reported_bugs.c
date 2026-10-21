/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Reported Frontier defects, checked against this port. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "commander/status_internal.h"
#include "station/market_internal.h"
#include "chart/galactic_map.h"
#include "world/hyperspace.h"
#include "market_blob.h"
#include "status_tables_data.h"

static void ignore_format(void *opaque, uint16_t text, const fe2_call_args *args, char out[FE2_TEXT_BYTES])
{
    (void)opaque; (void)text; (void)args;
    out[0] = 0;
}

static void ignore_message(void *opaque, uint16_t text, const fe2_call_args *args)
{
    (void)opaque; (void)text; (void)args;
}

static void ignore_music(void *opaque, uint16_t track) { (void)opaque; (void)track; }

static void ignore_event(void *opaque, uint16_t event, const fe2_call_args *args)
{
    (void)opaque; (void)event; (void)args;
}

static void ignore_name(void *opaque, uint16_t mode, uint32_t seed, char out[FE2_TEXT_BYTES])
{
    (void)opaque; (void)mode; (void)seed;
    out[0] = 0;
}

void fe2_status_contracts_changed(const fe2_game_services *game)
{
    (void)game;
}

static void services(fe2_game_services *game)
{
    memset(game, 0, sizeof *game);
    game->format_text = ignore_format;
    game->show_message = ignore_message;
    game->select_music = ignore_music;
    game->post_event = ignore_event;
    game->generate_name = ignore_name;
}

/* A failed mission costs the navy that issued it. */
static void failed_mission_demotes_its_own_navy(void)
{
    static fe2_commander_status status;
    fe2_game_services game;
    unsigned type, federal = 0, imperial = 0;
    services(&game);
    for (type = 1; type <= 6; ++type) {
        if (fe2_status_mission_authority[type] == 0x36) federal = type;
        else imperial = type;
    }
    assert(federal != 0 && imperial != 0);

    memset(&status, 0, sizeof status);
    status.federal_points = 1000;
    status.imperial_points = 1000;
    status.current_system = 1;
    status.day = 10;
    status.military_count = 1;
    status.military[0].type = (uint8_t)federal;
    status.military[0].home_system = 1;
    status.military[0].deadline = 1;
    fe2_status_settle_missions(&status, &game);
    assert(status.federal_points == 995);
    assert(status.imperial_points == 1000);

    memset(&status, 0, sizeof status);
    status.federal_points = 1000;
    status.imperial_points = 1000;
    status.current_system = 1;
    status.day = 10;
    status.military_count = 1;
    status.military[0].type = (uint8_t)imperial;
    status.military[0].home_system = 1;
    status.military[0].deadline = 1;
    fe2_status_settle_missions(&status, &game);
    assert(status.imperial_points == 995);
    assert(status.federal_points == 1000);
    printf("A failed mission demotes its own navy\n");
}

/* Passengers who abandon the ship free the cabins they occupied. */
static void abandoned_passengers_free_cabins(void)
{
    static fe2_commander_status status;
    fe2_game_services game;
    services(&game);
    memset(&status, 0, sizeof status);
    status.contract_count = 1;
    status.contracts[0].client = 1;
    status.contracts[0].kind = FE2_CONTRACT_EXPIRED;
    status.contracts[0].group = 0;
    status.cabins_used = 1;
    status.reliability = 5;
    fe2_status_scan_arrivals(&status, &game);
    assert(status.contract_count == 0);
    assert(status.cabins_used == 0);

    memset(&status, 0, sizeof status);
    status.contract_count = 1;
    status.contracts[0].client = 1;
    status.contracts[0].kind = FE2_CONTRACT_EXPIRED;
    status.contracts[0].group = (uint8_t)(3u << 2);
    status.cabins_used = 4;
    status.reliability = 5;
    fe2_status_scan_arrivals(&status, &game);
    assert(status.contract_count == 0);
    assert(status.cabins_used == 0);
    printf("Abandoned passengers free their cabins\n");
}

static uint32_t next_price_sample;

static uint32_t price_sample(void *opaque, uint32_t *high)
{
    (void)opaque; (void)high;
    return next_price_sample;
}

/* A positive base driven through zero is free. Radioactives and rubbish
 * stay negative, so the station pays to have them taken. */
static void export_prices_are_not_negative(void)
{
    static fe2_market_state state;
    fe2_market_services game;
    unsigned population, metadata, sample, commodity;
    assert(fe2_market_bind(fe2_test_market, sizeof fe2_test_market) == 0);
    memset(&game, 0, sizeof game);
    game.random = price_sample;
    for (population = 0; population < 10; ++population) {
        for (metadata = 0; metadata < 8; ++metadata) {
            for (sample = 0; sample < 0x400; sample += 0x80) {
                memset(&state, 0, sizeof state);
                state.draft.lookup = population * 2u;
                state.draft.system = metadata * 2u;
                next_price_sample = sample;
                for (commodity = 0; commodity < FE2_COMMODITIES; ++commodity)
                    state.trade_codes[commodity] = 0x0f;
                fe2_market_set_prices(&state, &game, 0);
                for (commodity = 0; commodity < FE2_COMMODITIES; ++commodity) {
                    uint16_t price = fe2_market_word(&state, 4u * commodity + 2u);
                    int waste = commodity >= FE2_COMMODITIES - 2;
                    assert(waste ? (price & 0x8000u) != 0 : (price & 0x8000u) == 0);
                }
            }
        }
    }
    printf("Only waste has a negative price\n");
}

static fe2_ship_stats hull_of(void *opaque, uint16_t model)
{
    fe2_ship_stats stats;
    (void)opaque; (void)model;
    stats.hull_mass = 10;
    stats.drive_limit = 1000;
    return stats;
}

/* A jump of 655.36 light years plus a short hop is the long jump. */
static void far_jump_does_not_wrap(void)
{
    fe2_hyperspace_state state;
    fe2_hyperspace_services game;
    fe2_drive_class classes[FE2_DRIVE_CLASSES];
    fe2_object ship;
    fe2_drive_query query;
    memset(&state, 0, sizeof state);
    memset(&game, 0, sizeof game);
    memset(classes, 0, sizeof classes);
    memset(&ship, 0, sizeof ship);
    memset(&query, 0, sizeof query);
    classes[1].range_factor = 50000;
    state.drive_classes = classes;
    state.player_handle = 1;
    state.hydrogen_fuel = 3;
    game.ship_stats = hull_of;
    ship.handle = 1;
    ship.as.craft.drive = 1;

    query.mode = FE2_DRIVE_PREPARE;
    query.request = 0;
    assert(fe2_hyperspace_query(&state, &game, &ship, &query) == FE2_HYPERSPACE_OK);
    assert(query.mode == 0xffff);

    memset(&query, 0, sizeof query);
    query.mode = FE2_DRIVE_PREPARE;
    query.request = 65536u + 100u;
    assert(fe2_hyperspace_query(&state, &game, &ship, &query) == FE2_HYPERSPACE_OK);
    assert(query.mode == 0xffff);

    memset(&query, 0, sizeof query);
    query.mode = FE2_DRIVE_PREPARE;
    query.request = 100;
    assert(fe2_hyperspace_query(&state, &game, &ship, &query) == FE2_HYPERSPACE_OK);
    assert(query.mode == FE2_DRIVE_PREPARE);
    assert(state.hydrogen_fuel == 3);
    printf("A far jump does not wrap\n");
}

/* Military fuel becomes radioactives of the same mass. */
static void military_fuel_keeps_its_cargo(void)
{
    fe2_hyperspace_state state;
    fe2_hyperspace_services game;
    fe2_drive_class classes[FE2_DRIVE_CLASSES];
    fe2_object ship;
    fe2_drive_query query;
    memset(&state, 0, sizeof state);
    memset(&game, 0, sizeof game);
    memset(classes, 0, sizeof classes);
    memset(&ship, 0, sizeof ship);
    memset(&query, 0, sizeof query);
    classes[10].range_factor = 50000;
    state.drive_classes = classes;
    state.player_handle = 1;
    state.military_fuel = 5;
    state.cargo_used = 5;
    game.ship_stats = hull_of;
    ship.handle = 1;
    ship.as.craft.drive = 10;
    query.mode = FE2_DRIVE_CONSUME;
    query.request = 100;
    assert(fe2_hyperspace_query(&state, &game, &ship, &query) == FE2_HYPERSPACE_OK);
    assert(query.mode == FE2_DRIVE_CONSUME);
    assert(state.military_fuel + state.radioactives == 5);
    assert(state.cargo_used == 5);
    printf("Military fuel keeps its cargo\n");
}

/* A second system on the same map position can still be jumped to. */
static void coincident_systems_can_be_jumped(void)
{
    assert(fe2_map_jump_distance(0, 2, 1) == 1);
    assert(fe2_map_jump_distance(0, 1, 1) == 0);
    assert(fe2_map_jump_distance(40, 2, 1) == 40);
    printf("A coincident system is one jump unit away\n");
}

int main(void)
{
    failed_mission_demotes_its_own_navy();
    abandoned_passengers_free_cabins();
    export_prices_are_not_negative();
    far_jump_does_not_wrap();
    military_fuel_keeps_its_cargo();
    coincident_systems_can_be_jumped();
    return 0;
}
