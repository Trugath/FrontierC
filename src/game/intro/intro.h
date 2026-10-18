/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_INTRO_INTRO_H
#define FE2_INTRO_INTRO_H

/*
 * Intro presentation (intro): the title sequence's timed scenes and the
 * timed credit overlays.
 *
 * Each render picks the scene whose window holds the intro time. It writes
 * the scene's animation phase into the scene object and renders that object
 * with the module's model and text banks. Between scene 0 and scene 1 an
 * orbit interval turns the render vector. Past the last scene the timeline
 * restarts.
 *
 * The scene objects are native object records (FE2_INTRO_OBJECT_BYTES)
 * because the renderer reads the whole record. Banks, rendering, overlays and
 * music come through `fe2_intro_services`.
 */
#include <stdint.h>

#include "game/call_args.h"
#include "game/text_table.h"

enum {
    FE2_INTRO_SCENES = 22,
    FE2_INTRO_OBJECT_BYTES = 0x11e,
    FE2_INTRO_PHASE_AT = 0x72,        /* object word the scene animates by */
    FE2_INTRO_MUSIC_FLAG = 0x20       /* state flags  bit 5 */
};

/* A scene's window of intro time and its phase: (time - start) * 16 /
 * divisor + bias. The timeline compiler rejects windows whose quotient
 * could exceed 16 bits. */
typedef struct fe2_intro_window {
    uint32_t start, end;
    uint16_t divisor, bias;
} fe2_intro_window;

typedef struct fe2_intro_state {
    uint32_t time;
    uint8_t overlay_mode;             /* 0 shows the panel sprite, not credits */
    uint8_t flags;
    uint16_t render_vector[3];
    uint32_t classification[4];
    uint8_t return_mode;
    const int16_t *sine;              /* resident table: sin by angle >> 4 */
    /* The module's scene objects (loaded with the module; phases written). */
    uint8_t orbit_scene[FE2_INTRO_OBJECT_BYTES];
    uint8_t scenes[FE2_INTRO_SCENES][FE2_INTRO_OBJECT_BYTES];
} fe2_intro_state;

typedef struct fe2_intro_services {
    void *opaque;
    /* Models (the bank's directory) and texts for the renderer. */
    void (*register_bank)(void *opaque, const uint8_t *models, const fe2_text_table *texts,
                          uint16_t bank);
    void (*release_bank)(void *opaque);
    /* Render the object record in parameter 9. */
    void (*render_object)(void *opaque, uint8_t *record, const fe2_call_args *args);
    void (*clear_overlay)(void *opaque);
    void (*restart_video)(void *opaque);
    void (*begin_overlay)(void *opaque);
    void (*draw_sprite)(void *opaque, uint16_t sprite, uint16_t x, uint16_t y);
    void (*draw_text)(void *opaque, uint16_t text, uint8_t colour, uint16_t x, uint16_t y,
                      const fe2_call_args *args);
    void (*check_music)(void *opaque);
    void (*select_music)(void *opaque, uint16_t track);
} fe2_intro_services;

/* Load the scene objects (as loading the overlay does). */
void fe2_intro_load(fe2_intro_state *state);
/* 0x8a: render the scene for the current time; returns the return mode. */
uint8_t fe2_intro_render(fe2_intro_state *state, const fe2_intro_services *game);
/* 0x7e: the timed copyright, credits and start-menu text. */
void fe2_intro_timed_overlay(fe2_intro_state *state, const fe2_intro_services *game);

#endif
