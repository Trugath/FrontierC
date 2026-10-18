/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_HOST_GAME_HOST_H
#define FE2_HOST_GAME_HOST_H
/*
 * The platform-independent part of a game host: the session's frame,
 * video and colour services and intro's bank, rendering, sprite and text
 * providers, on the native renderer (render/runtime.h). A platform
 * supplies the resources, the renderer and a present callback.
 */
#include "render/runtime.h"
#include "session/session.h"
#include "intro/intro.h"

enum { FE2_GAME_HOST_SPRITE_BYTES = 65536 };

typedef struct fe2_game_host {
    fe2_render_runtime *runtime;
    fe2_render_runtime_resources *resources;
    fe2_session *session;
    void *gameplay;                    /* optional native gameplay service context */
    fe2_video_present_fn present;
    void *present_context;
    /* Pump the window while a modal editor waits. Nonzero means stop. */
    int (*pump)(void *context);
    fe2_render_color_state colour;
    fe2_render_color_services colour_services;
    fe2_render_bytes saved_models[240];  /* the main bank while an overlay's bank is registered */
    fe2_text_directory saved_module;     /* text.module while a bank is registered */
    int bank_registered;
    int module_saved;
    /* Module texts stay until the next begin_frame: label expand runs in
     * execute_frame after release_bank restores the model bank. */
    int module_pending_release;
    int in_frame;                        /* between begin_frame and present */
    /* The portrait writes DAC 0x80+ after the scene has assigned those
     * entries. The next presented frame that does not upload over them
     * puts the scene colours back. */
    int dac_clobbered;
    int palette_frame;
    uint8_t colour_map[256];             /* the text colour map (identity) */
    uint8_t sprite_work[FE2_GAME_HOST_SPRITE_BYTES];
    unsigned renders, errors;
} fe2_game_host;

void fe2_game_host_init(fe2_game_host *host, fe2_render_runtime *runtime, fe2_render_runtime_resources *resources,
                        fe2_video_present_fn present, void *present_context);
/* Fills the session services the host provides (context = host); the
 * session must call fe2_game_host_attach after creation. */
void fe2_game_host_session_services(fe2_game_host *host, fe2_session_services *services);
void fe2_game_host_attach(fe2_game_host *host, fe2_session *session);
/* Resident 0x15c/0x160: restore the saved colour and publish the frame mode.
 * With clear_view, 0x15c first clears the viewport through service 0x1dc. */
void fe2_game_host_restore_color(fe2_game_host *host, int clear_view);
/* intro's providers (opaque = host). */
void fe2_game_host_intro_services(fe2_game_host *host, fe2_intro_services *services);
/* render an object record (the original's 0x11e-byte layout) in its
 * explicit frame: its basis (+0) and camera offset (+0x14), model +0x5a and
 * variables from +0x72, the session's light  and the shades. */
int fe2_game_host_render_record(fe2_game_host *host, const uint8_t *record);
#endif
