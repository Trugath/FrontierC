/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_MENU_OVERLAYS_H
#define FE2_SESSION_MENU_OVERLAYS_H
#include "commander_overlay.h"
#include "station_overlay.h"
/*
 * commander (preferences, time and commander files, overlay 10) and station (the
 * station screen and the flight messages, overlay 12) bound to a session.
 *
 * commander's entries: +0x4 initialize, +0x8 (activate) pause or preferences,
 * +0x10 (update) the time keys and menu input, +0x14 (draw) the menu and
 * file dialog input, +0x20 (message) restore the time step, +0x24
 * (deselect) resume. Its restart (JMP FAR 0x44) is fe2_session_restart and
 * its quit (JMP FAR 0x40, AX 0) the exit scene's mode 0, both deferred.
 *
 * station's entries: +0x4 initialize, +0x8 (activate) the station screen,
 * +0xc the events, +0x10 (update) the station input, +0x14 (draw) the
 * selection menu, +0x20 (message) show a message (the objects layer's
 * messages: selector, parameter 7, value 1, amount 3), +0x24 (deselect)
 * leave the screen.
 *
 * Native services: the overlay selection, input, the
 * pointer regions, events, the
 * exit scene, random numbers below a bound  and music. The
 * session's scalar globals the overlays read (time, flags, steps, the
 * event time, the input mask, music, the commander's name, the hold
 * count, suppression, light) are carried into their states and back
 * around each entry and service. Other globals the overlays share with
 * each other (flight's flight mode, the player's cargo and ship as station's
 * guest references) are not; their hosts keep them consistent.
 */
#endif
