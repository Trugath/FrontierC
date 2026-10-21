/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Shared session for the headless gameplay checks. */
#ifndef FE2_GAMEPLAY_HARNESS_H
#define FE2_GAMEPLAY_HARNESS_H
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "host/asset_bind.h"
#include "host/gameplay.h"
#include "game/object_data.h"
#include "pool/pool.h"
#include "session/intro_overlay.h"
#include "mining/mining.h"
#include "station/market.h"
#include <stddef.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern fe2_gameplay game;
extern fe2_game_host host;
extern fe2_session session;
extern fe2_session_intro_overlay intro;
extern fe2_render_runtime_resources resources;
extern fe2_render_runtime *runtime;
extern const fe2_object *ship;
extern unsigned frames, ink, restarts;
extern uint16_t selection;
extern int check_argc;
extern char **check_argv;

void snapshot(const char *name);
int sentence_key(void *context);
unsigned occupied(const fe2_session *s);
unsigned population(const fe2_session *s);
unsigned patrols(const fe2_session *s);
int64_t station_clearance(const fe2_object *ship, const fe2_object *station, int linked);
void frame(void);
unsigned view_ink(void);
void commit_offence(uint16_t offence);
unsigned flight_picture(const uint8_t *page);
int row_has(const fe2_gameplay *g, uint16_t text);
void station_open(void);
void station_region(uint8_t code);
void station_row(unsigned row);
void click_icon(unsigned x, unsigned y);
void plant_stale_request_head(void);
uint16_t mining_action(uint16_t action);
void gameplay_loops(void);
int basis_step(const int16_t *a, const int16_t *b);
void station_frame(void);
int fe2_check_scanner(void);
int fe2_check_dust(void);
int fe2_check_views(void);
int fe2_check_hud(void);
int fe2_check_fire(void);
int fe2_check_crash(void);
int fe2_check_view(void);
int fe2_check_autopilot(void);
int fe2_check_arcturus(void);
int fe2_check_navigation(void);
int fe2_check_lave(void);
int fe2_check_jump(void);
int fe2_check_dest(void);
int fe2_check_fine(void);
int fe2_check_deliver(void);
int fe2_check_spawn(void);
int fe2_check_ports(void);
int fe2_check_police(void);
int fe2_check_shipyard(void);
int fe2_check_loops(void);
int fe2_check_mission(void);
int fe2_check_station(void);
void fe2_check_intro(void);
int fe2_check_original(void);
int fe2_check_commander(void);
int fe2_check_ui(void);
int fe2_check_shipinfo(void);
int fe2_check_scene(void);
int fe2_check_far(void);
int fe2_check_exit(void);
int fe2_check_infophone(void);
int fe2_check_screens(void);

#endif
