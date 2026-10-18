/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_MARKET_OVERLAY_H
#define FE2_SESSION_MARKET_OVERLAY_H
#include "objects.h"
#include "station/market.h"
/*
 * market (commodities, markets and reference objects) bound to a session as
 * overlay 7, with the resident object services of a bound
 * `fe2_session_objects`.
 *
 * Entries: +0x4 initialize (0x6e: AL=1 starts a game) and +0xc the events
 * (0x76: a new system and a new day). market's other vectors (0x82 store
 * motion, 0x8a reference objects, 0x92 commodity names) are called by other
 * overlays, not by the session: they sit where the session expects +0x18,
 * +0x20 and +0x28, so they are not session entries. 0x8a is the `market`
 * entry of the session's overlay calls (overlay_calls.h, context 7):
 * reference service BX with the handle in AL, the actor and target in
 * parameters 8 and 9, the scratch parameters in and out, and for request
 * 0 the market record found. 0x82 and 0x92 are used through the port with
 * `state` and `services` directly.
 *
 * Native services: the object visitor, lookup, random
 * numbers  and music. The station message goes through
 * the session's overlay calls when station is registered. The catalogue and chart
 * queries, the text and number formatters (and the station message without
 * station) are the host's, from `host`.
 *
 * The session, its objects layer and the resident store own the globals
 * market also reads (market count, trade codes, day, system, player, flight
 * state, music and the object list): the binding carries them into the
 * market state and back around each entry and each native service.
 */
typedef struct fe2_session_market_overlay {
    fe2_session *session;
    fe2_session_objects *objects;
    fe2_market_state state;
    fe2_market_services host;
    fe2_market_services services;
    unsigned depth;                 /* entries running */
} fe2_session_market_overlay;

int fe2_session_bind_market_overlay(fe2_session *session, fe2_session_objects *objects,
                                    fe2_session_market_overlay *binding);
#endif
