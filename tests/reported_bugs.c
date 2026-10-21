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

/* Export discounts do not pay the commander to remove the goods. */
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
                    assert((price & 0x8000u) == 0);
                }
            }
        }
    }
    printf("Export prices are not negative\n");
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
    coincident_systems_can_be_jumped();
    return 0;
}
