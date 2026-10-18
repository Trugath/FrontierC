/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_SYSTEMS_OVERLAY_H
#define FE2_SESSION_SYSTEMS_OVERLAY_H
#include "objects.h"
/* system_record.h's FE2_RECORD_BYTES (a 64-byte system record) clashes
 * with pool.h's (a 0x11e-byte object record): rename systems's while its
 * header is read. */
#define FE2_RECORD_BYTES FE2_SYSTEM_RECORD_BYTES
#include "world/systems.h"
#undef FE2_RECORD_BYTES
/*
 * systems (star system generation and instantiation) bound to a session as
 * overlay 0.
 *
 * systems is a library: its vector table (image 0x6a) has only +0x20 (0x8a, the
 * object action: generate a system into a record space or instantiate its
 * records) and +0x28 (0x92, format systems text). Neither is a session entry:
 * +0x20 takes registers and an argument block, not a message, and the
 * session has no +0x28; the other overlays call them. So no session entry
 * is routed and the two run through fe2_session_systems_action and
 * fe2_session_systems_format_text. +0x20 is also the overlay call `system`
 * (overlay_calls.h): the request comes from its parameters, the records
 * from parameter 8 (its value the record count; 0xf44 bytes when unset).
 *
 * Other overlays: names's names through the overlay call `name` when it is
 * registered, otherwise the host's `generate_name`.
 *
 * Native services: slot flags  and the two bases  from
 * the generation's basis angles. Everything else (the resident math, the
 * allocation from systems's native template, object references, models, text
 * and names's names) is the host's, from `host`.
 *
 * systems has no resident-annotated state; the generation values live in
 * `generation` across calls.
 */
typedef struct fe2_session_systems_overlay {
    fe2_session *session;
    fe2_system_generation generation;
    fe2_system_services host;
    fe2_system_services services;
    unsigned depth;                 /* entries running */
} fe2_session_systems_overlay;

int fe2_session_bind_systems_overlay(fe2_session *session, fe2_session_systems_overlay *binding);
/* 0x8a and 0x92. */
void fe2_session_systems_action(fe2_session_systems_overlay *binding, fe2_record_space space,
                                const fe2_system_request *request);
void fe2_session_systems_format_text(fe2_session_systems_overlay *binding, uint16_t selector);
#endif
