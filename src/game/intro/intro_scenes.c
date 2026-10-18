/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* 0x8a: the timed scenes, the orbit interval and the timeline restart. */
#include <string.h>

#include "intro_tables.h"

enum {
    MODEL_BANK = 8, MODEL_DIRECTORY_AT = 2,
    RESTART_TIME = 0x400, INTRO_MUSIC = 8,
    SCENE_PARAMETER = 9,
    ORBIT_PHASE_SHIFT = 10,
    ORBIT_TURN_START = 0x140, ORBIT_TURN_STEPS = 0x3f, ORBIT_TURN_CENTRE = 0x20,
    ORBIT_ANGLE_STEP = 0x180, QUARTER_TURN = 0x4000,
    ORBIT_SCALE = 0x5a82, ORBIT_HEIGHT = 0xa57e
};

void fe2_intro_load(fe2_intro_state *s)
{
    if (!fe2_intro_orbit_scene || !fe2_intro_scenes) return;
    memcpy(s->orbit_scene, fe2_intro_orbit_scene, sizeof s->orbit_scene);
    memcpy(s->scenes, fe2_intro_scenes, sizeof s->scenes);
}

static void set_phase(uint8_t *record, uint16_t phase)
{
    record[FE2_INTRO_PHASE_AT] = (uint8_t)phase;
    record[FE2_INTRO_PHASE_AT + 1] = (uint8_t)(phase >> 8);
}

static int in_window(const fe2_intro_window *w, uint32_t time)
{
    return time >= w->start && time < w->end;
}

/* The scene's phase. The original divides with DIV after publishing a
 * continuation for the resident divide handler; the timeline keeps the
 * quotient within 16 bits. */
static uint16_t scene_phase(const fe2_intro_window *w, uint32_t time)
{
    uint32_t scaled = (time - w->start) << 4;
    return (uint16_t)((uint16_t)(scaled / w->divisor) + w->bias);
}

/* Signed table sine: angle bit 15 negates; the rest index by angle >> 4. */
static int16_t sine(const fe2_intro_state *s, uint16_t angle)
{
    int16_t value = s->sine[(angle & 0x7fffu) >> 4];
    return (angle & 0x8000u) ? (int16_t)(0u - (uint16_t)value) : value;
}

static uint16_t scaled_high(int16_t value)
{
    return (uint16_t)((uint32_t)((int32_t)ORBIT_SCALE * value) >> 16);
}

/*
 * Orbit interval. The orbit scene's phase is the elapsed time / 1024.
 * - Half that phase, modulo 0x200, is a step; steps 0x140..0x17f turn the view.
 *   Below 0x140 the step is 0, and above 0x17f it holds at 0x3f.
 * - The angle is (step - 0x20) * 0x180.
 * - The render vector is 0x5a82 (sqrt 1/2) times the sine of the angle, a
 *   fixed 0xa57e, and 0x5a82 times the sine a quarter turn on.
 */
static void orbit(fe2_intro_state *s, uint32_t elapsed)
{
    uint32_t phase = elapsed >> ORBIT_PHASE_SHIFT;
    uint16_t step = (uint16_t)((phase >> 1) & 0x1ff);
    uint16_t angle;
    set_phase(s->orbit_scene, (uint16_t)phase);
    if (step < ORBIT_TURN_START) step = 0;
    else if ((uint16_t)(step - ORBIT_TURN_START) > ORBIT_TURN_STEPS) step = ORBIT_TURN_STEPS;
    else step = (uint16_t)(step - ORBIT_TURN_START);
    angle = (uint16_t)((int16_t)(step - ORBIT_TURN_CENTRE) * ORBIT_ANGLE_STEP);
    s->render_vector[0] = scaled_high(sine(s, angle));
    s->render_vector[1] = ORBIT_HEIGHT;
    s->render_vector[2] = scaled_high(sine(s, (uint16_t)(angle + QUARTER_TURN)));
}

/* Select the scene for the current time; returns its record, having set its
 * phase and (outside the orbit) the initial render vector. Null past the
 * last scene. */
static uint8_t *select_scene(fe2_intro_state *s)
{
    uint32_t time = s->time;
    unsigned scene;
    if (in_window(&fe2_intro_scene_windows[0], time)) {
        set_phase(s->scenes[0], scene_phase(&fe2_intro_scene_windows[0], time));
        memcpy(s->render_vector, fe2_intro_render_vector, sizeof s->render_vector);
        return s->scenes[0];
    }
    time -= fe2_intro_orbit_start;
    if (time < fe2_intro_orbit_length) {
        orbit(s, time);
        return s->orbit_scene;
    }
    time -= fe2_intro_orbit_length;
    for (scene = 1; scene < FE2_INTRO_SCENES; ++scene) {
        const fe2_intro_window *w = &fe2_intro_scene_windows[scene];
        if (!in_window(w, time)) continue;
        set_phase(s->scenes[scene], scene_phase(w, time));
        memcpy(s->render_vector, fe2_intro_render_vector, sizeof s->render_vector);
        return s->scenes[scene];
    }
    return 0;
}

static void restart(fe2_intro_state *s, const fe2_intro_services *game)
{
    s->time = RESTART_TIME;
    game->clear_overlay(game->opaque);
    game->restart_video(game->opaque);
    game->clear_overlay(game->opaque);
    game->select_music(game->opaque, INTRO_MUSIC);
    s->flags |= FE2_INTRO_MUSIC_FLAG;
}

uint8_t fe2_intro_render(fe2_intro_state *s, const fe2_intro_services *game)
{
    fe2_call_args args;
    uint8_t *scene;
    game->register_bank(game->opaque, fe2_intro_model_bank + MODEL_DIRECTORY_AT,
                        (const fe2_text_table *)fe2_intro_text_directory, MODEL_BANK);
    /* The restart services may move the time again. */
    while ((scene = select_scene(s)) == 0) restart(s, game);
    memcpy(s->classification, fe2_intro_classification, sizeof s->classification);
    fe2_args_clear(&args);
    fe2_args_template(&args, SCENE_PARAMETER, scene);
    game->render_object(game->opaque, scene, &args);
    game->release_bank(game->opaque);
    return s->return_mode;
}
