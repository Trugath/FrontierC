/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_INTERNAL_H
#define FE2_SESSION_INTERNAL_H
#include "session.h"
/* Shared between session.c and frontend.c. */
enum { FE2_SESSION_PENDING_RESTART = 1, FE2_SESSION_PENDING_EXIT_SCENE = 2 };
fe2_session_overlay *fe2_session_lock(fe2_session *session, unsigned slot);
void fe2_session_reset_overlays(fe2_session *session);
int fe2_session_begin_exit_scene(fe2_session *session, uint16_t mode);
fe2_session_result fe2_session_frontend_step(fe2_session *session);
/* Run deferred restarts and exit scenes until none is requested. */
int fe2_session_unwind(fe2_session *session);
#endif
