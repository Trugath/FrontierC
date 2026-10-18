/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_NAVIGATION_OVERLAY_H
#define FE2_SESSION_NAVIGATION_OVERLAY_H
#include "objects.h"
#include "flight/navigation.h"
/*
 * navigation (the navigation map) bound to a session as overlay 5, with the
 * resident object services of a bound `fe2_session_objects`.
 *
 * Entries: +0x4 initialize (0x6e; the selection's low byte is the request,
 * its high byte the default-object mode), +0x8 activate (0x72, the current
 * system), +0x10 update (0x7a, keys and the map render), +0x14 draw (0x7e, the
 * time point and target labels), +0x18 the periodic record (0x82, the motion
 * sample), +0x20 message (0x8a, a generated system) and +0x24 deselect (0x8e,
 * restore). The time-point text (0x92, +0x28) has no session entry: callers
 * reach it through fe2_navigation_time_point.
 *
 * Native services: the overlay selection, input, the
 * pointer regions, the pool records (0x8c release, 0xb4
 * lookup, handle flags, the 0x0/0x4 visitors) and the basis.
 * The station message (`message`), the chart current system (`map` request 0)
 * and the systems system action (`system`) go through the session's overlay
 * calls (overlay_calls.h) when registered; the systems call gets the root
 * kind, key and danger chart's reply left in the shared parameters
 * (`system_reply`). Everything else (drawing, rendering, models, motion,
 * the other math and the calls without a provider) is the host's, from
 * `host`.
 *
 * The session and the objects layer own the resident globals navigation reads
 * (time, autopilot target, flight state, ship, pointer, keys, colours,
 * the lists): the binding carries them into the navigation state and back
 * around each entry and each native service.
 */
typedef struct fe2_session_navigation_overlay {
    fe2_session *session;
    fe2_session_objects *objects;
    fe2_navigation_state state;
    fe2_navigation_services host;
    fe2_navigation_services services;
    fe2_motion_samples samples;     /* what the last 0x82 published  */
    fe2_call_args system_reply;     /* the parameters chart's request 0 left */
    unsigned depth;                 /* entries running */
} fe2_session_navigation_overlay;

int fe2_session_bind_navigation_overlay(fe2_session *session, fe2_session_objects *objects,
                                        fe2_session_navigation_overlay *binding);
#endif
