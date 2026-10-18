/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_HYPERSPACE_OVERLAY_H
#define FE2_SESSION_HYPERSPACE_OVERLAY_H
#include "objects.h"
#include "world/hyperspace.h"
/*
 * drives (hyperspace) bound to a session as overlay 16, with the resident
 * object services of a bound `fe2_session_objects`.
 *
 * drives has two entries: +0x8 (0x72, `activate`) the jump and +0x20 (0x8a,
 * `message`) the drive query. The original's 0x8a takes its mode in AX, the
 * model in BX (mode 3) and its inputs in the scratch dwords 0x7bc..0x7d0,
 * none of which the session's message entry carries: callers fill `query`
 * (and `model` for mode 3) before the entry, which then dispatches on
 * query.mode (0 prepare, 3 class ranges into `ranges`, 4 nothing, others
 * the query for `subject`, the ship when null). The jump's query results
 * are left in `query` too. Other overlays reach 0x8a through the session's
 * overlay call `drive` (overlay_calls.h, context 16), which carries the
 * registers and the scratch as its request, value and parameters, and
 * +0x8 through `jump`.
 *
 * Native services: events, slot flags  and random numbers
 *. The chart target and the station message go through the
 * session's overlay calls when registered. Ship statistics, the
 * systems system action, clearing objects, interpolation, sound
 *, the misjump  and the calls without a provider are the
 * host's, from `host`.
 *
 * The session and the objects layer own the resident globals drives reads
 * (player, cargo, clocks, records, systems, pools); the binding carries
 * them into the hyperspace state and back around each entry and native
 * service.
 */
typedef struct fe2_session_hyperspace_overlay {
    fe2_session *session;
    fe2_session_objects *objects;
    fe2_hyperspace_state state;
    fe2_hyperspace_services host;
    fe2_hyperspace_services services;
    /* The 0x8a registers and scratch (see above) and its last result. */
    fe2_drive_query query;
    uint16_t model;
    fe2_object *subject;
    uint32_t ranges[FE2_RANGED_CLASSES];
    int result;
    unsigned depth;                 /* entries running */
    /* chart request 0 and the following systems action share 0x7c8/0x7d4/0x7d8.
     * The root kind, key and danger stay here for the system action. */
    fe2_call_args system_reply;
} fe2_session_hyperspace_overlay;

int fe2_session_bind_hyperspace_overlay(fe2_session *session, fe2_session_objects *objects,
                                        fe2_session_hyperspace_overlay *binding);
#endif
