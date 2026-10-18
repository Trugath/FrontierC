/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SYSTEMS_SYSTEMS_INTERNAL_H
#define FE2_SYSTEMS_SYSTEMS_INTERNAL_H

/* Shared declarations between the systems translation units. */
#include "systems.h"
#include "system_tables.h"

/* Profile sizes from 0x1036640 up are suns and gas giants that take the
 * system's own name; smaller bodies are named after their parent. */
enum { FE2_NAMED_BODY_SIZE = 0x01036640, FE2_MAX_GENERATED_RECORDS = 60,
       FE2_MAX_STATIONS = 0x12 };

fe2_system_record *fe2_systems_record(fe2_system_generation *g, unsigned index);
uint32_t fe2_systems_position(unsigned index, unsigned offset);

/* generation_random.c */
/* advance both random states; returns state A. */
uint32_t fe2_systems_random(fe2_system_generation *g);
/* profile curve at `position`, saturating at 0xffff from 0x4000. */
uint16_t fe2_systems_curve(uint16_t position);

/* body_names.c */
/* names name of `type` from the record's seed, into its name field. */
void fe2_systems_generate_name(fe2_system_generation *g, unsigned record, uint16_t type);
/* the name of a new profile record. */
void fe2_systems_name_profile(fe2_system_generation *g, unsigned record);

/* profiles.c */
/* create the profile for g->value at *record. Returns 0 when the
 * profile has no model (nothing is finalized); rules may append station
 * records and move *record to the last one. */
int fe2_systems_create_profile(fe2_system_generation *g, unsigned *record);
/* scalar, identity, ancestor correction and rule of a record. */
void fe2_systems_finalize_profile(fe2_system_generation *g, unsigned *record);
/* sqrt(m^3 / parent) for a body of normalized scalar `scalar` around a
 * parent of normalized size `parent`, through the resident services with
 * their truncating doubled products. */
fe2_scalar fe2_systems_orbit_root(fe2_system_generation *g, fe2_scalar scalar, fe2_scalar parent);
/* Final 0x5edb scaling of that root by the resident multiply. */
fe2_scalar fe2_systems_orbit_scale(fe2_system_generation *g, fe2_scalar root);

/* profile_rules.c */
fe2_profile_rule fe2_systems_profile_rule(unsigned profile);

/* stations.c */
/* up to three generated stations; `scale` and `base` as in rules. */
void fe2_systems_append_stations(fe2_system_generation *g, unsigned *record, uint16_t scale,
                             uint16_t base);
/* one Sol-style station: 0 Daedalus, 1 Galileo, other Abraham Lincoln. */
void fe2_systems_append_sol_station(fe2_system_generation *g, unsigned *record, uint16_t choice);

/* children.c / root_groups.c / handcrafted.c / instantiation.c */
void fe2_systems_generate_children(fe2_system_generation *g);
void fe2_systems_generate_root_group(fe2_system_generation *g, uint32_t size);
void fe2_systems_load_handcrafted(fe2_system_generation *g);
void fe2_systems_instantiate(fe2_system_generation *g, const uint32_t orbit[2]);

#endif
