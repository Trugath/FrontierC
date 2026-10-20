/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_M11_OVERLAY_H
#define FE2_SESSION_M11_OVERLAY_H
#include "objects.h"
#include "people/objects.h"
/*
 * people (system objects: creation, events, updates) bound to a session as
 * overlay 11, with the resident object services of a bound
 * `fe2_session_objects`.
 *
 * Entries: +0x4 initialize (0x6e, the selection's low byte the request),
 * +0xc the events (0x76), +0x18 the periodic record (0x82, the time the
 * motion sample), +0x1c the second record (0x86, the interval the step) and
 * +0x30 record events (0x9a, the record's handle and the request as the
 * form). +0x20 (0x8a, creation) takes the argument block and BX, which a
 * session message does not carry: it is fe2_session_people_create.
 *
 * Native services: visitors, lookup, allocation (0xa8,
 * 0xac), quiet release, slot flags, damage, the
 * flagged and nearby effects, equipment slots and launches
 *, timed effects, random numbers, the
 * basis, events  and music. Everything else is
 * the host's, from `host`.
 *
 * The other overlays (chart, catalogue, market, names, commander, station, offences, drives) are the
 * session's registered entries (overlay_calls.h) when there are any, and
 * the host's otherwise. people registers population_create (0x8a with BX
 * and the caller's parameters); the update entry (+0x1c) needs the object and the
 * step, which population_update does not carry, so it is not registered.
 *
 * The session and the objects layer own the resident globals people reads
 * (the ship, the pools, time, cargo, the system and the rest): the
 * binding carries them into the people state and back around each entry and
 * each native service.
 */
typedef struct fe2_session_people_overlay {
    fe2_session *session;
    fe2_session_objects *objects;
    fe2_people_state state;
    fe2_people_services host;
    fe2_people_services services;
    fe2_call_args args;             /*, the module's working block */
    unsigned depth;                 /* entries running */
} fe2_session_people_overlay;

int fe2_session_bind_people_overlay(fe2_session *session, fe2_session_objects *objects,
                                 fe2_session_people_overlay *binding);
/* 0x8a: create by the mode in parameter 7 of binding->args; `parent` is BX.
 * Returns the entry's result (modes 5..8: 1 when made, parameter 9). */
uint16_t fe2_session_people_create(fe2_session_people_overlay *binding, uint16_t parent);
#endif
