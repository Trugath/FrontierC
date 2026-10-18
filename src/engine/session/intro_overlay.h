/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_INTRO_OVERLAY_H
#define FE2_SESSION_INTRO_OVERLAY_H
#include "session.h"
#include "intro/intro.h"
/*
 * intro (the intro presentation) bound to a session as overlay 8: its entry
 * +0x20 (the session's `message`, called with no message) renders the scene
 * for the intro time (fe2_intro_render), and +0x14 (`draw`) draws the timed
 * copyright, credits and start-menu text (fe2_intro_timed_overlay).
 *
 * The session owns the intro time (the clock's low word, 0x1fc1), the state
 * flags, the render vector (0x1fe1, `light`) and Space's key state
 * (0x8741, intro's return mode) and the drawing flag (0x1fce, intro's overlay
 * mode: 0x158 sets it, clearing the screen clears it); the binding carries
 * them into the intro state and back around each entry. The
 * classification values  stay in the intro state. The host supplies
 * the bank, rendering, sprite, text and overlay-start providers in `host`;
 * clearing, presenting  and the music come from the session.
 */
typedef struct fe2_session_intro_overlay {
    fe2_session *session;
    fe2_intro_state state;
    fe2_intro_services host;
    fe2_intro_services services;
    uint8_t return_mode;            /* the last render's result */
} fe2_session_intro_overlay;

int fe2_session_bind_intro_overlay(fe2_session *session, fe2_session_intro_overlay *binding);
#endif
