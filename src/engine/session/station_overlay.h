/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_STATION_OVERLAY_H
#define FE2_SESSION_STATION_OVERLAY_H
#include "objects.h"
#include "station/station.h"
/* station names records by 32-bit references; natively they are numbers the
 * host resolves: a pool handle for the current system's records, the
 * handle plus FE2_STATION_REF_MAP for the second pool's, that
 * pool as a list (parameter 12) by FE2_STATION_REF_MAP_LIST, and the work
 * records by FE2_STATION_REF_WORK times one more than their number, and
 * the preview's name  by FE2_STATION_REF_PREVIEW_NAME. The
 * name of a record is its reference plus 0xf6, as the original's. */
enum {
    FE2_STATION_REF_MAP = 0x100, FE2_STATION_REF_MAP_LIST = 0x400, FE2_STATION_REF_PREVIEW_NAME = 0x800,
    FE2_STATION_REF_WORK = 0x1000
};

/* station bound to a session as overlay 12 (see menu_overlays.h). */
typedef struct fe2_session_station_overlay {
    fe2_session *session;
    fe2_session_objects *objects;
    fe2_station_state state;
    fe2_station_services host;
    fe2_station_services services;
    fe2_call_args args;             /* the argument block the entries see */
    unsigned depth;
    /* The registration  and ship name  work records
     * names last named, which texts read through their references. */
    char names[2][FE2_STATION_NAME];
} fe2_session_station_overlay;

int fe2_session_bind_station_overlay(fe2_session *session, fe2_session_objects *objects,
                                     fe2_session_station_overlay *binding);
/* Show message `text` with the call arguments the offence and flight paths pass. */
void fe2_session_station_notify(fe2_session_station_overlay *binding, uint16_t text,
                                const fe2_call_args *args);
/* panels's +0x24 notification carries a request and consumes its return value. */
uint16_t fe2_session_station_resume(fe2_session_station_overlay *binding, int16_t request);
#endif
