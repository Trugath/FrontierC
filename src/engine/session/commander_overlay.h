/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_M10_OVERLAY_H
#define FE2_SESSION_M10_OVERLAY_H
#include "objects.h"
#include "commander/preferences.h"
/* commander bound to a session as overlay 10 (see menu_overlays.h). */
typedef struct fe2_session_commander_overlay {
    fe2_session *session;
    fe2_session_objects *objects;
    fe2_commander_state state;
    fe2_commander_services host;
    fe2_commander_services services;
    unsigned depth;
} fe2_session_commander_overlay;

int fe2_session_bind_commander_overlay(fe2_session *session, fe2_session_objects *objects, fe2_session_commander_overlay *binding);
#endif
