/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_HOST_GAMEPLAY_H
#define FE2_HOST_GAMEPLAY_H
#include "game_host.h"
#include "intro_audio.h"
#include "session/systems_overlay.h"
#include "session/flight_overlay.h"
#include "session/orbits.h"
#include "session/panels_overlay.h"
#include "session/offences_overlay.h"
#include "session/station_overlay.h"
#include "session/people_overlay.h"
#include "session/commander_overlay.h"
#include "session/map_overlay.h"
#include "session/status_overlay.h"
#include "session/navigation_overlay.h"
#include "session/market_overlay.h"
#include "session/hyperspace_overlay.h"
#include "session/mining_overlay.h"
#include "session/library_calls.h"
#include "commander/dos_host.h"
#include "position/position.h"
#include "motion/motion.h"
#include "contact/contact.h"
#include "scene_probe/scene_probe.h"
#include "station/market.h"

/* Native game services joining the session, world, flight engine and renderer.
 * Storage must remain stable, like the session it belongs to. */
typedef struct fe2_gameplay {
    fe2_game_host *host;
    fe2_session *session;
    fe2_intro_audio *audio;
    fe2_session_objects objects;
    fe2_session_systems_overlay systems;
    fe2_session_flight_overlay flight;
    fe2_session_orbits orbits;
    fe2_session_panels_overlay panels;
    fe2_session_offences_overlay offences;
    fe2_session_station_overlay station;
    fe2_session_people_overlay population;
    fe2_session_commander_overlay menu;
    fe2_session_map_overlay map;
    fe2_session_status_overlay status;
    fe2_dos_host files;
    fe2_dos_platform file_services;
    fe2_position position;
    fe2_reference reference;
    fe2_motion motion;
    fe2_motion_services motion_services;
    fe2_contact contact;
    fe2_contact_services contact_services;
    fe2_scene_probe probe;
    fe2_scene_probe_services probe_services;
    fe2_session_navigation_overlay navigation;
    uint8_t navigation_text[65536];
    fe2_system_record navigation_records[256];
    fe2_system_generation navigation_generation;
    void (*navigation_draw_native)(void *, fe2_session *);
    unsigned navigation_scenes, navigation_projections;
    fe2_session_market_overlay market;
    fe2_session_hyperspace_overlay hyperspace;
    fe2_session_mining_overlay mining;
    fe2_session_catalogue catalogue;
    fe2_session_names names;
    fe2_session_portraits portraits;
    fe2_session_interaction_overlay interaction;
    fe2_model_header system_models[240];
    fe2_flight_model flight_models[240];
    fe2_system_record system_records[256];
    fe2_system_record map_records[256];
    uint8_t map_page[FE2_VIDEO_FRAME_BYTES], map_palette[256][3];
    uint8_t map_page_valid;
    uint8_t map_saved_palette[128][3], map_palette_saved;
    void (*map_draw_native)(void *, fe2_session *);
    void (*flight_draw_native)(void *, fe2_session *);
    void (*station_draw_native)(void *, fe2_session *);
    uint8_t station_ship_pixels[115 * 320], station_ship_valid;
    uint16_t pointer_x, pointer_y;
    uint16_t mouse_x, mouse_y;
    uint16_t sampled_x, sampled_y; /* last mouse position already given to the pointer */
    uint8_t mouse_buttons;
    uint8_t mouse_click;           /* a primary press not yet sampled */
    unsigned bodies, scenes, submitted, beams, map_pixels;
    unsigned previews, preview_pixels;
    unsigned distant, frame_models;
    uint8_t map_cleared;
    fe2_station_model station_models[240];    /* station's 0x50 answers by model / 2 */
    unsigned text_failures;                   /* texts drawn as their selector */
    /* station's work records: the preview ship, and the systems a mission
     * and an offer generate (0xf44 bytes of system records each). */
    fe2_object station_preview;
    /* The commander file's image (fe2_save_image): the object block, the
     * overlay records and table, DS 0x1fc1..0x55ca and the stack slice. Bytes
     * no native field holds keep what the last loaded file had. */
    struct {
        uint8_t objects[FE2_SAVE_OBJECT_BYTES];
        uint8_t overlay_records[8 * FE2_SAVE_OVERLAY_RECORDS];
        uint8_t table[4 * FE2_SAVE_TABLE_DWORDS];
        uint8_t resident[FE2_SAVE_RESIDENT_END - FE2_SAVE_RESIDENT_START];
        uint8_t stack[FE2_SAVE_STACK_END - FE2_SAVE_STACK_START];
    } image;
    uint8_t image_retake;                     /* the save event follows the image */
    uint8_t station_systems[2][0xf44];
    char place_name[32];
    int active;
} fe2_gameplay;

int fe2_gameplay_start(fe2_gameplay *game, fe2_game_host *host, fe2_intro_audio *audio,
                       uint16_t selection);
/* The host's pending input is transferred into the native session input queue. */
void fe2_gameplay_key(fe2_gameplay *game, uint8_t scan, int extended, int released);
void fe2_gameplay_mouse(fe2_gameplay *game, unsigned x, unsigned y, unsigned buttons);
int fe2_gameplay_render_scene(fe2_gameplay *game, fe2_object *camera);
int fe2_gameplay_render_navigation(fe2_gameplay *game, fe2_object *camera, fe2_object *object);
void fe2_gameplay_model_variables(const fe2_object *object, uint16_t out[86]);
#endif
