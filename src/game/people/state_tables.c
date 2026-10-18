/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The behaviour tables (image 0xa3 and 0x12b): per behaviour the update
 * (0x86) and motion (0x82) handlers, and the relationship handlers for a
 * pool walk and a direct event (0x9a). */
#include "people_internal.h"

#define NONE_U people_update_none
#define NONE_M people_motion_none

const people_behaviour people_behaviours[FE2_PEOPLE_BEHAVIOURS] = {
    {people_update_escort_parent, people_common_motion, NONE_U, people_assign_patrol_parent},          /* 00 */
    {people_update_patrol_parent, people_common_motion, people_reroute_on_parent_event, people_turn_and_track}, /* 04 */
    {people_update_patrol_parent, people_motion_patrol_and_activate_slot, people_reroute_on_parent_event, people_turn_and_track},           /* 08 */
    {people_update_track_parent, people_common_motion, people_reroute_on_parent_event, people_turn_and_track},  /* 0C */
    {people_update_approach_parent, people_common_motion, people_reroute_on_parent_event, people_turn_and_track},  /* 10 */
    {people_update_choose_patrol_offset, people_common_motion, people_reroute_on_parent_event, people_turn_and_track},  /* 14 */
    {people_update_consume_frame_capacity, people_common_motion, NONE_U, people_report_player_offence_once},             /* 18 */
    {people_update_leave_parent, people_common_motion, NONE_U, people_release_market_and_assign_parent},             /* 1C */
    {people_update_adopt_target_as_parent, people_common_motion, NONE_U, people_assign_patrol_parent},          /* 20 */
    {people_update_report_escort_arrival, people_common_motion, NONE_U, people_turn_and_track},          /* 24 */
    {people_update_spin_and_emit_effects, people_common_motion, people_reroute_to_event_target, NONE_U},                 /* 28 */
    {NONE_U, NONE_M, NONE_U, people_report_player_offence_once},                       /* 2C */
    {NONE_U, NONE_M, NONE_U, people_report_player_offence_once},                       /* 30 */
    {NONE_U, NONE_M, NONE_U, people_report_player_offence_once},                       /* 34 */
    {people_update_offset_and_wait_for_event, NONE_M, NONE_U, NONE_U},                                 /* 38 */
    {people_update_start_arrival_countdown, NONE_M, NONE_U, NONE_U},                                 /* 3C */
    {people_update_complete_arrival, NONE_M, NONE_U, NONE_U},                                 /* 40 */
    {people_update_home_missile, people_motion_integrate_homing_missile, people_expire_or_untarget_missile, NONE_U},                          /* 44 */
    {people_update_stationary_projectile, people_motion_apply_projectile_force, people_expire_or_untarget_missile, NONE_U},                          /* 48 */
    {people_update_countdown_to_drift, people_motion_emit_trail_and_drift, NONE_U, NONE_U},                                  /* 4C */
    {people_update_remove_distant_cargo, people_motion_drift_and_collect_cargo, NONE_U, NONE_U},                                  /* 50 */
    {people_update_remove_distant_effect, NONE_M, NONE_U, NONE_U},                                 /* 54 */
    {people_update_remove_distant_emission, people_motion_rotate_from_seed, NONE_U, NONE_U},                                  /* 58 */
    {NONE_U, people_motion_expire_countdown_or_distance, NONE_U, NONE_U},                                 /* 5C */
    {people_update_expire_timed_effect, NONE_M, NONE_U, NONE_U},                                 /* 60 */
    {NONE_U, people_motion_burning_wreck, NONE_U, NONE_U},                                 /* 64 */
    {people_update_expire_hidden_craft, NONE_M, NONE_U, NONE_U},                                 /* 68 */
    {people_update_expire_launch_flash, NONE_M, NONE_U, NONE_U},                                 /* 6C */
    {people_update_request_departure, people_common_motion, NONE_U, people_grant_departure_clearance},             /* 70 */
    {people_update_dock_at_parent, people_motion_follow_docking_parent, NONE_U, NONE_U},                                  /* 74 */
    {people_update_launch_from_parent, NONE_M, NONE_U, NONE_U},                                 /* 78 */
    {people_update_wait_for_station_clearance, NONE_M, NONE_U, NONE_U},                                 /* 7C */
    {people_update_return_to_parent, people_common_motion, NONE_U, people_report_player_offence_once},             /* 80 */
    {people_update_wait_for_departure, NONE_M, NONE_U, people_depart_immediately}                         /* 84 */
};
