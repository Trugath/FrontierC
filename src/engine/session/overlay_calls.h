/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_OVERLAY_CALLS_H
#define FE2_SESSION_OVERLAY_CALLS_H
/*
 * The overlays' direct entries into each other, as the session routes them.
 *
 * In the original an overlay calls another through the overlay call
 * (its entry +0x20, +0x28 or +0x8a) with registers and the shared parameter
 * block (0x7bc + 4n). Natively a bound overlay registers the entries it
 * provides here, with its binding as the context (`context[module]`), and
 * a caller's binding routes its cross-overlay service through the entry
 * when one is registered, and to the host's provider otherwise.
 *
 * The parameter block is `args`, in and out: an entry leaves its results
 * in the parameters as the original does, and each caller reads its own
 * reply from them. Values the original returns in registers are results
 * or named out-parameters.
 *
 * This header includes only the shared game types, so any binding can
 * include it beside its own overlay's header.
 */
#include <stddef.h>
#include <stdint.h>
#include "game/call_args.h"
#include "game/object.h"
#include "game/text_table.h"

enum { FE2_OVERLAY_CALL_MODULES = 20 };

/* A star system as chart and systems name it: its seeds (DX:AX, CX:BX), its
 * name and its reference. */
typedef struct fe2_overlay_system {
    uint32_t seed_a, seed_b;
    const char *name;
    uint32_t reference;
} fe2_overlay_system;

/* What catalogue's trade query (request 1) returns in registers besides AX:
 * the market profile, government and 31 trade codes. */
typedef struct fe2_overlay_trade {
    uint16_t metadata;
    uint16_t government;
    const uint8_t *codes;
} fe2_overlay_trade;

/* A resolved record and its opaque reference. */
typedef struct fe2_overlay_record {
    uint8_t *record;
    uint32_t ref;
} fe2_overlay_record;

typedef struct fe2_overlay_calls {
    void *context[FE2_OVERLAY_CALL_MODULES];      /* each provider's binding */

    /* systems: generate (parameter 2 = 0) or instantiate (1) the system of
     * the seeds into the record at parameter 8; the seeds in and out. */
    void (*system)(void *context, fe2_overlay_system *system, fe2_call_args *args);
    /* chart: request 0 the current or target system (into `system`; may be
     * null for other requests); 1 and 2 the relationship queries. */
    uint16_t (*map)(void *context, uint16_t request, fe2_call_args *args, fe2_overlay_system *system);
    /* flight: activate the cockpit view (+0x8, 0x72) and the capacity reset (0x8a). */
    void (*flight_view)(void *context);
    void (*flight_reset_capacity)(void *context);
    /* panels: panel request `request` with `value` (the view or the widgets). */
    void (*panel)(void *context, uint16_t request, uint16_t value, fe2_call_args *args);
    /* catalogue: the catalogue queries (0 the population counts, 1 a rank, and
     * the system profile); request 1's
     * register results into `trade` when not null. */
    uint16_t (*catalogue)(void *context, uint16_t request, fe2_call_args *args,
                          fe2_overlay_trade *trade);
    /* market: service `request` with handle `handle`; the record it points
     * at into `record` when not null. */
    uint16_t (*market)(void *context, uint16_t request, uint16_t handle, fe2_call_args *args,
                       fe2_overlay_record *record);
    /* names: the name of `type` from `seed`, NUL-terminated. */
    void (*name)(void *context, uint16_t type, uint32_t seed, char *out, size_t capacity);
    /* commander: the time-step reset, restoring the time step, and the alarm check. */
    void (*menu_reset)(void *context);
    void (*menu_restore_time)(void *context);
    uint8_t (*menu_check)(void *context);
    /* people: create a craft (parameters as the original's), and the update
     * entry (+0x1c, 0x86) of `object` over `step` (DX:AX), returning the
     * object it leaves in parameter 9. */
    fe2_object *(*population_create)(void *context, fe2_call_args *args);
    fe2_object *(*population_update)(void *context, fe2_object *object, uint32_t step);
    /* station: show message `text`. */
    void (*message)(void *context, uint16_t text, const fe2_call_args *args);
    /* composites: draw the portrait of `identity`. */
    void (*portrait)(void *context, uint32_t identity, const fe2_call_args *args);
    /* status: add the contract in the parameters. */
    void (*contract)(void *context, const fe2_call_args *args);
    /* offences: an event about parameter 8's object, a police request, and
     * committing an offence. */
    void (*offence_event)(void *context, uint16_t event, const fe2_call_args *args);
    void (*police)(void *context, uint16_t request, const fe2_call_args *args);
    void (*commit_offence)(void *context, uint16_t offence);
    /* drives: drive request `request` with `value` (the query's mode, the
     * arrival, the ship's model), and the jump (+0x8). */
    uint16_t (*drive)(void *context, uint16_t request, uint16_t value, fe2_call_args *args);
    void (*jump)(void *context);
    /* mining: mining request `request`. */
    uint16_t (*mining)(void *context, uint16_t request, fe2_call_args *args);
    /* interaction: compose interaction text `selector`. */
    void (*interaction_text)(void *context, uint16_t selector, fe2_text_out *out);
} fe2_overlay_calls;

/* The registered entry's context: FE2_OVERLAY_CONTEXT(s, 12). */
#define FE2_OVERLAY_CONTEXT(session, module) ((session)->calls.context[(module)])
#endif
