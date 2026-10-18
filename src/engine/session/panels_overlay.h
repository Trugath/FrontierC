/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_PANELS_OVERLAY_H
#define FE2_SESSION_PANELS_OVERLAY_H
#include "objects.h"
#include "flight/panels.h"
/*
 * panels (the control panel strip and scanner markers) bound to a session as
 * overlay 4, with the objects layer (the ship is its player record).
 *
 * Entries: +0x4 initialize (0x6e, AL the request), +0xc the events (0x76:
 * the clock and date), +0x10 update (0x7a: scanner markers and gauges), +0x20
 * message (0x8a: select a panel, AX the message's selector) and +0x2c the
 * coarse tick (0x96: refresh the panel).
 *
 * Other overlays select panels through the overlay call `panel`
 * (overlay_calls.h): the request is AX, the value BX and parameter 2 the
 * argument. panels calls no other overlay but the selected panel's +0x24.
 *
 * Native services: the input put back, the object visitor
 * and 0x158's drawing flag. Everything else is the host's, from `host`,
 * including the old selection's +0x24 (`notify_panel`, which needs AL back).
 *
 * The session and the objects layer own the resident globals panels reads
 * (drawing flag, panel selection, clock, gauges, palette indices and the
 * rest): the binding carries them into the panel state and back around
 * each entry and each native service.
 */
typedef struct fe2_session_panels_overlay {
    fe2_session *session;
    fe2_session_objects *objects;
    fe2_panel_state state;
    fe2_panel_services host;
    fe2_panel_services services;
    /* The original's registers and argument words the entries see: +0x20's
     * BX (the overlay whose panel is selected) and 0x7c4, the request
     * when no message is given, and 0x7a's 0x7c0 and 0x7dc. */
    uint16_t overlay, argument;
    int16_t request;
    uint16_t view_flags;
    fe2_object *viewer;
    unsigned depth;                 /* entries running */
} fe2_session_panels_overlay;

int fe2_session_bind_panels_overlay(fe2_session *session, fe2_session_objects *objects,
                                    fe2_session_panels_overlay *binding);
#endif
