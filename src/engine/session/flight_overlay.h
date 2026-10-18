/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_FLIGHT_OVERLAY_H
#define FE2_SESSION_FLIGHT_OVERLAY_H
#include "objects.h"
#include "flight/flight.h"
/*
 * flight (flight and cockpit) bound to a session as overlay 3, with the
 * resident object services of a bound `fe2_session_objects`.
 *
 * Entries: +0x4 initialize, +0x8 activate (the cockpit view), +0xc the
 * events, +0x10 update (the cockpit pass), +0x14 draw (the status pass),
 * +0x18/+0x1c the periodic and second records (flight update and timers),
 * +0x20 message (the capacity reset), +0x24 deselect (change view 0), +0x2c
 * the coarse tick (the countdown) and +0x30 record events (select target,
 * with the request as the form).
 *
 * Native services: the overlay selection, damage, the
 * flagged and nearby effects, capacity, equipment
 * slots and launches, the exit scene, input (0x48,
 * 0x1c0), the pointer regions, slot flags, events
 *, timed effects, random numbers, servicing,
 * the basis  and music. Everything else is the host's,
 * from `host`.
 *
 * The other overlays (panels, market, names..station, offences..mining) go through the
 * session's entries (`session->calls`) when registered, otherwise to
 * `host`. flight registers its cockpit view and capacity reset there.
 *
 * The session and the objects layer own the resident globals flight also
 * reads (time, flags, keys, pointer buttons, axes, player, cargo, rating
 * and the rest): the binding carries them into the flight state and back
 * around each entry and each native service.
 */
typedef struct fe2_session_flight_overlay {
    fe2_flight_record mission_records[6];
    fe2_session *session;
    fe2_session_objects *objects;
    fe2_flight_state state;
    fe2_flight_services host;
    fe2_flight_services services;
    /* The original's registers and argument words the entries leave for
     * the next: the interval 0x1cc got (0x28's AX), event 0x11's handle
     *  and the record pointer the cockpit and status passes see
     * (0x7dc; the ship when null). */
    uint32_t interval;
    uint8_t selected;
    fe2_object *subject;
    unsigned depth;                 /* entries running */
} fe2_session_flight_overlay;

int fe2_session_bind_flight_overlay(fe2_session *session, fe2_session_objects *objects,
                                    fe2_session_flight_overlay *binding);
#endif
