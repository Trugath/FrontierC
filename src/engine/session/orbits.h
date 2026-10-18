/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_ORBITS_H
#define FE2_SESSION_ORBITS_H
#include "session.h"
#include "world/orbits.h"
/* The resident session owns pool/time routing and native math. Message and
 * docking-release providers are required only when a claim expires. */
typedef struct fe2_session_orbits {
    fe2_session *session;
    fe2_orbit_state state;
    fe2_orbit_services services;
    void *context;
    void (*message)(void *context, uint16_t text, const fe2_call_args *args);
    void (*reference)(void *context, uint16_t request, uint8_t handle, const fe2_call_args *args);
} fe2_session_orbits;
int fe2_session_bind_orbits(fe2_session *session, fe2_session_orbits *binding);
#endif
