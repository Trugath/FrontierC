/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_STATUS_OVERLAY_H
#define FE2_SESSION_STATUS_OVERLAY_H
#include "objects.h"
/* The objects layer already names military fuel (20, as status does). */
#define FE2_CARGO_MILITARY_FUEL FE2_STATUS_CARGO_MILITARY_FUEL
#include "commander/status.h"
#undef FE2_CARGO_MILITARY_FUEL
/*
 * status (the commander status screens, contracts, crew and military
 * standing) bound to a session as overlay 14, with the resident object
 * services of a bound `fe2_session_objects`.
 *
 * Entries: +0x4 initialize (0x6e, a new commander), +0x8 activate (0x72,
 * open and draw the status screen), +0xc the events (0x76), +0x10 update
 * (0x7a, one key), +0x1c the second record (0x86, maybe a child object beside
 * the record), +0x24 deselect (0x8e, back to the status screen) and +0x2c the
 * coarse tick (0x96, the arrival scan). +0x14, +0x18 and +0x30 are unused.
 * +0x20 (0x8a) accepts a contract from the argument block and +0x28 (0x92)
 * formats status text; the session has neither argument, so they are the
 * calls below instead. 0x8a is also the `contract` entry of the session's
 * overlay calls (overlay_calls.h, context 14), the contract decoded from
 * the parameters.
 *
 * Native services: the overlay selection, the panel's region reset
 *, input, events, random numbers,
 * allocation, lookup, the object visitor, the basis
 * (0x2dc, from the view angles) and music. The station message, the
 * panels panel, the market reference service and people creation go
 * through the session's overlay calls when registered. Everything else
 * (and those without a provider) is the host's, from `host`.
 *
 * status has no generated resident sync: the binding carries by hand the
 * commander state's DS globals the session, its objects layer and the
 * shared resident store own (clock, cash, capacity, cargo, rating, the
 * ship, the object list, the counts the other overlays read and the rest)
 * into the state and back around each entry and native service. status's own
 * lists (contracts, military, crew, mining) stay in `state`.
 */
typedef struct fe2_session_status_overlay {
    fe2_session *session;
    fe2_session_objects *objects;
    fe2_commander_status state;
    fe2_game_services host;
    fe2_game_services services;
    uint32_t mix;                   /* 's EDX */
    uint8_t key;                    /* the last key 0x48 gave, for 0x1c0 */
    int result;                     /* the last drawing entry's FE2_STATUS_* */
    unsigned depth;                 /* entries running */
} fe2_session_status_overlay;

int fe2_session_bind_status_overlay(fe2_session *session, fe2_session_objects *objects,
                                    fe2_session_status_overlay *binding);
/* 0x8a: accept `contract` (as the argument block gives it). */
void fe2_session_status_add_contract(fe2_session_status_overlay *binding, const fe2_contract *contract);
/* 0x92: format status text `selector`. */
void fe2_session_status_format_text(fe2_session_status_overlay *binding, uint16_t selector);
#endif
