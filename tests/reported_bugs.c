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

int main(void)
{
    failed_mission_demotes_its_own_navy();
    return 0;
}
