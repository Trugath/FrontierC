/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_OBJECTS_H
#define FE2_SESSION_OBJECTS_H
#include "session.h"
/*
 * The resident object services bound to a session. The session supplies
 * the pool (allocation, release, lookup and status), overlay messages
 * (+0x20), selection notices (+0x24), damage notices (+0x30) and broadcasts;
 * the host supplies the rest in `host` (the probe, model bank, sound,
 * basis, move, text, input and drawing). A missing host provider fails
 * the frame when its path is used.
 *
 * The session owns the globals both sides use: the random words
 * (`noise`), the time step, the overlay selection and the player's free
 * capacity. The calls below carry them into `state` and back.
 */
typedef struct fe2_session_objects {
    fe2_session *session;
    fe2_objects_state state;
    fe2_objects_services host;     /* the host's providers (and their context) */
    fe2_objects_services services; /* bound by fe2_session_bind_objects */
} fe2_session_objects;

/* Also routes the pool's nearby-effect damage to fe2_objects_damage. */
int fe2_session_bind_objects(fe2_session *session, fe2_session_objects *binding);

uint32_t fe2_session_objects_random(fe2_session_objects *binding, uint32_t *mix);
uint16_t fe2_session_objects_random_below(fe2_session_objects *binding, uint16_t range);
void fe2_session_objects_select_overlay(fe2_session_objects *binding, uint16_t slot);
/* returns the fuel consumed (AL: the capacity used, for people). */
uint32_t fe2_session_objects_update(fe2_session_objects *binding, fe2_object *record, uint16_t quantum);
void fe2_session_objects_consume(fe2_session_objects *binding, fe2_object *record, uint32_t amount);
void fe2_session_objects_spawn_effect(fe2_session_objects *binding, fe2_object *source, const uint32_t position[6],
                                      uint16_t flags);
fe2_object *fe2_session_objects_deploy(fe2_session_objects *binding, fe2_object *source, uint16_t slot,
                                       uint8_t target);
void fe2_session_objects_service(fe2_session_objects *binding, fe2_object *record, uint32_t amount);
void fe2_session_objects_activate(fe2_session_objects *binding, fe2_object *record, int8_t index);
int fe2_session_objects_damage(fe2_session_objects *binding, fe2_object *record, uint16_t amount, uint16_t attacker);
void fe2_session_objects_edit_text(fe2_session_objects *binding, char *text, uint16_t capacity, uint16_t argument,
                                   uint16_t mode);
#endif
