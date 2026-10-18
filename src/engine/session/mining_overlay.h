/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_MINING_OVERLAY_H
#define FE2_SESSION_MINING_OVERLAY_H
#include "objects.h"
#include "mining/mining.h"
/*
 * mining (mining machines) bound to a session as overlay 17, with the
 * resident object services of a bound `fe2_session_objects`.
 *
 * Entries: +0x4 initialize (0x6e, no records), +0xc the events (0x76: the
 * hopper tick and the machines' recreation), +0x18 the periodic record
 * (0x82, deploy/recover progress), +0x20 message (0x8a, the actions: the
 * selector is AX) and +0x30 record events (0x9a, removing a machine's
 * record). 0x92 (the mining text, +0x28) has no session entry: it is
 * reached through fe2_session_mining_text. Other overlays reach 0x8a
 * through the session's overlay call `mining` (overlay_calls.h, context
 * 17).
 *
 * Native services: the record allocation, lookup, the visit
 *, random numbers  and events. Everything else
 * (positions, linking, release through 0x84, the station message and the
 * text formatter) is the host's, from `host`; the catalogue profile and the offences
 * offence go through the session's overlay calls when registered, the
 * host's otherwise.
 *
 * The session and the objects layer own the resident globals mining also
 * reads (the record counts, the current system, the frame step, cargo,
 * the ship and its flags, the flight state and the object lists): the
 * binding carries them into the mining state and back around each entry
 * and each service.
 */
typedef struct fe2_session_mining_overlay {
    fe2_session *session;
    fe2_session_objects *objects;
    fe2_mining_state state;
    fe2_mining_services host;
    fe2_mining_services services;
    int action_result;              /* the last 0x8a's AX (nonzero: 0xffff) */
    unsigned depth;                 /* entries running */
} fe2_session_mining_overlay;

int fe2_session_bind_mining_overlay(fe2_session *session, fe2_session_objects *objects,
                                    fe2_session_mining_overlay *binding);
/* 0x92: mining text `selector`. */
void fe2_session_mining_text(fe2_session_mining_overlay *binding, uint16_t selector, fe2_text_out *out);
#endif
