/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_OFFENCES_OVERLAY_H
#define FE2_SESSION_OFFENCES_OVERLAY_H
#include "objects.h"
#include "people/offences.h"
/*
 * offences (offences and police) bound to a session as overlay 15, with the
 * resident object services of a bound `fe2_session_objects`.
 *
 * Entries: +0xc the events (0x76; the value comes back unchanged), +0x10
 * update (0x7a, the police source's launch progress), +0x20 message (0x8a,
 * commit offence `selector`: the objects layer's panel message 3 is the
 * first weapons warning) and +0x2c the coarse tick (0x96, the launches).
 * +0x28 (0x92, the offence text) has no session entry: other overlays reach
 * it through fe2_offence_text directly. +0x20 is also the session's
 * overlay calls `offence_event`, `police` and `commit_offence`
 * (overlay_calls.h, context 15): all commit offence AX by the object in
 * parameter 9.
 *
 * Native services: random numbers, the object visitor
 * and music. The market slot vector, people creation and station
 * message go through the session's overlay calls when registered; they
 * (without a provider), the sound effect  and the text formatter
 *  are the host's, from `host`.
 *
 * The session and the objects layer own the resident globals offences reads
 * (police flags and phase, the source, fines, scores, the player, the
 * time step, the lists and the rest): the binding carries them into the
 * offence state and back around each entry and each native service.
 */
typedef struct fe2_session_offences_overlay {
    fe2_session *session;
    fe2_session_objects *objects;
    fe2_offence_state state;
    fe2_offence_services host;
    fe2_offence_services services;
    /* The scratch subject the events and commits see; the player's
     * record when null. */
    fe2_object *subject;
    uint32_t mix;                   /* EDX of the last 0x12c call */
    unsigned depth;                 /* entries running */
} fe2_session_offences_overlay;

int fe2_session_bind_offences_overlay(fe2_session *session, fe2_session_objects *objects,
                                      fe2_session_offences_overlay *binding);
#endif
