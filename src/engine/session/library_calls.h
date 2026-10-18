/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_LIBRARY_CALLS_H
#define FE2_SESSION_LIBRARY_CALLS_H
#include "session.h"
#include "world/system_catalogue.h"
#include "world/names.h"
#include "intro/composite.h"
#include "station/interaction_text.h"
/*
 * The library overlays bound to a session as providers of the overlay
 * calls (overlay_calls.h). catalogue, names, composites and interaction have no session entries:
 * the other overlays call them directly, so each binding only registers
 * its entry and itself as `calls.context[module]`.
 */

/*
 * catalogue (system catalogue), `calls.catalogue`: the 0x8a query with AL as
 * `request` and the system key in parameter 1. Results are word
 * stores into the parameters, as the original's scratch words:
 * - 0, the profile: field 0x5, the map text (replacing the key), the view
 *   text, fields 0x6..0x9 and the government in parameters 0..7. people's
 *   population counts (0..5) and mining's system profile (field 0x5 in
 *   parameter 0) are both this request. The result is field 0x5.
 * - 1, the trade data: the result is the population band (AX, people's
 *   rank), and parameter 2 the government. BX (field 0xb) and the
 *   trade codes have no place in the call and are not returned.
 * - any other, the summary: the strengths, the larger and the allegiance
 *   in parameters 0..3, and the resident pair (0x20f1, 0x20f3, from the
 *   host; zeros when it has none) ordered into parameters 7 and 6. The
 *   result is zero.
 */
typedef struct fe2_catalogue_host {
    void *opaque;
    void (*resident_pair)(void *opaque, uint16_t *first, uint16_t *second);
} fe2_catalogue_host;

typedef struct fe2_session_catalogue {
    fe2_session *session;
    const fe2_system_catalogue *catalogue;  /* fe2_default_catalogue unless set before binding */
    fe2_catalogue_host host;
} fe2_session_catalogue;

int fe2_session_bind_catalogue(fe2_session *session, fe2_session_catalogue *binding);

/*
 * names (names), `calls.name`: 0xf92, the name of `type` from the seed's low
 * (AX) and high (DX) words. A name that does not fit `capacity` (29 bytes
 * hold every name) or an invalid type leaves an empty name and fails the
 * frame. Names keep the native font bytes.
 */
typedef struct fe2_session_names {
    fe2_session *session;
} fe2_session_names;

int fe2_session_bind_names(fe2_session *session, fe2_session_names *binding);

/*
 * composites (composite graphics), `calls.portrait`: the public draw (0x8a, +0x20)
 * with the identity's low word in AX and high word in DX, and the override
 * from parameter 1 (0x7c0, station's 0x4b41). The drawing is the host's video
 * (`host`); without all three providers nothing is drawn. A provider's
 * error fails the frame. `composite` keeps the appearance.
 */
typedef struct fe2_session_portraits {
    fe2_session *session;
    fe2_composite composite;
    fe2_composite_video host;
} fe2_session_portraits;

int fe2_session_bind_portraits(fe2_session *session, fe2_session_portraits *binding);

/*
 * interaction (interaction text), `calls.interaction_text`: 0x92 (+0x28), the text
 * of `selector` through the host's text formatter (0x5c, from `host`).
 */
typedef struct fe2_session_interaction_overlay {
    fe2_session *session;
    fe2_interaction_services host;
    fe2_interaction_services services;
    unsigned depth;                 /* entries running */
} fe2_session_interaction_overlay;

int fe2_session_bind_interaction(fe2_session *session, fe2_session_interaction_overlay *binding);
#endif
