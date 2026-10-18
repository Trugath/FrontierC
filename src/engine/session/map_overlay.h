/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_MAP_OVERLAY_H
#define FE2_SESSION_MAP_OVERLAY_H
#include "objects.h"
/* system_record.h's FE2_RECORD_BYTES (a 64-byte system record) clashes
 * with pool.h's (a 0x11e-byte object record): rename systems's while the map's
 * header is read, as systems_overlay.h does. */
#define FE2_RECORD_BYTES FE2_SYSTEM_RECORD_BYTES
#include "chart/galactic_map.h"
#undef FE2_RECORD_BYTES
/*
 * chart (the galactic map) bound to a session as overlay 2, with the
 * resident object services of a bound `fe2_session_objects`.
 *
 * Entries: +0x4 initialize (AL=1 starts at the AH start position; other
 * selections leave the map alone), +0x8 activate, +0xc the events (1
 * promotes the selection to home, 0x14 perturbs home with `range` as DX),
 * +0x10 update (the pan keys, shift and the polled command), +0x14 draw (the
 * status text) and +0x24 deselect (enter view 0). +0x18/+0x1c are no-ops. The
 * map's +0x20 (query a system) takes registers the session's message cannot
 * supply: other overlays reach it through the overlay call `map`
 * (overlay_calls.h; request 0 the selected system, 1 a nearby candidate,
 * 2 and above the query of a packed reference). +0x28 (format text) stays
 * reachable only through the port API.
 *
 * Other overlays: drives's travel query (`drive`) and catalogue's profile
 * (`catalogue`, mode 0) through the overlay calls when registered,
 * otherwise the host's `travel` and `system_details`. systems's generation
 * (`generate_system`) stays the host's: the map reads systems's named-body
 * count, which the overlay call does not return.
 *
 * Native services: the overlay selection (0x8, `set_panel_mode`), input
 * (0x48's input code for `input_pending`, 0x1c0 for `forward_command`) and
 * random numbers. Everything else is the host's, from `host`:
 * the basis takes its angles from 0x134, which the port does not supply.
 *
 * The generated resident sync is empty (galactic_map.h carries no DS
 * annotations); the binding carries the player's ship  and the
 * current system  into the map state and back itself.
 */
typedef struct fe2_session_map_overlay {
    fe2_session *session;
    fe2_session_objects *objects;
    fe2_map_state state;
    fe2_map_services host;
    fe2_map_services services;
    uint16_t range;                 /* event 0x14's DX in, DX out */
    int status;                     /* the last entry's FE2_MAP_* result */
    char name[FE2_MAP_TEXT_CAPACITY]; /* the last queried name  */
    unsigned depth;                 /* entries running */
} fe2_session_map_overlay;

int fe2_session_bind_map_overlay(fe2_session *session, fe2_session_objects *objects,
                                 fe2_session_map_overlay *binding);
#endif
