/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
static void people_host_visit(void *opaque, fe2_object_list * p0, fe2_people_visitor p1, void * p2)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.visit) return;
    people_out(binding_); binding_->host.visit(binding_->host.opaque, p0, p1, p2); people_back(binding_);
}
static fe2_object * people_host_lookup(void *opaque, uint8_t p0, const fe2_call_args * p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    fe2_object * result_;
    if (!binding_->host.lookup) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.lookup(binding_->host.opaque, p0, p1); people_back(binding_);
    return result_;
}
static int people_host_allocate(void *opaque, const fe2_call_args * p0, fe2_object ** p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.allocate) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.allocate(binding_->host.opaque, p0, p1); people_back(binding_);
    return result_;
}
static int people_host_allocate_upper(void *opaque, const fe2_call_args * p0, fe2_object ** p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.allocate_upper) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.allocate_upper(binding_->host.opaque, p0, p1); people_back(binding_);
    return result_;
}
static void people_host_release(void *opaque, fe2_object * p0)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.release) return;
    people_out(binding_); binding_->host.release(binding_->host.opaque, p0); people_back(binding_);
}
static void people_host_remove(void *opaque, fe2_object * p0)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.remove) return;
    people_out(binding_); binding_->host.remove(binding_->host.opaque, p0); people_back(binding_);
}
static void people_host_attach(void *opaque, fe2_object * p0)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.attach) return;
    people_out(binding_); binding_->host.attach(binding_->host.opaque, p0); people_back(binding_);
}
static void people_host_set_slot_flags(void *opaque, fe2_object * p0, uint8_t p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.set_slot_flags) return;
    people_out(binding_); binding_->host.set_slot_flags(binding_->host.opaque, p0, p1); people_back(binding_);
}
static void people_host_setup(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.setup) return;
    people_out(binding_); binding_->host.setup(binding_->host.opaque, p0, p1); people_back(binding_);
}
static const uint8_t * people_host_model(void *opaque, uint16_t p0)
{
    fe2_session_people_overlay *binding_ = opaque;
    const uint8_t * result_;
    if (!binding_->host.model) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.model(binding_->host.opaque, p0); people_back(binding_);
    return result_;
}
static fe2_people_contacts people_host_scan_contacts(void *opaque, fe2_object * p0)
{
    fe2_session_people_overlay *binding_ = opaque;
    fe2_people_contacts result_;
    if (!binding_->host.scan_contacts) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.scan_contacts(binding_->host.opaque, p0); people_back(binding_);
    return result_;
}
static int people_host_damage(void *opaque, fe2_object * p0, uint16_t p1, const fe2_call_args * p2)
{
    fe2_session_people_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.damage) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.damage(binding_->host.opaque, p0, p1, p2); people_back(binding_);
    return result_;
}
static void people_host_activate_effect(void *opaque, fe2_object * p0)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.activate_effect) return;
    people_out(binding_); binding_->host.activate_effect(binding_->host.opaque, p0); people_back(binding_);
}
static void people_host_consume_effect(void *opaque, fe2_object * p0)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.consume_effect) return;
    people_out(binding_); binding_->host.consume_effect(binding_->host.opaque, p0); people_back(binding_);
}
static uint8_t people_host_capacity_used(void *opaque, fe2_object * p0, uint32_t p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    uint8_t result_;
    if (!binding_->host.capacity_used) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.capacity_used(binding_->host.opaque, p0, p1); people_back(binding_);
    return result_;
}
static void people_host_activate_slot(void *opaque, fe2_object * p0, uint8_t p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.activate_slot) return;
    people_out(binding_); binding_->host.activate_slot(binding_->host.opaque, p0, p1); people_back(binding_);
}
static int people_host_deploy_slot(void *opaque, fe2_object * p0, uint16_t p1, const fe2_call_args * p2)
{
    fe2_session_people_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.deploy_slot) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.deploy_slot(binding_->host.opaque, p0, p1, p2); people_back(binding_);
    return result_;
}
static void people_host_spawn_effect(void *opaque, const fe2_call_args * p0)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.spawn_effect) return;
    people_out(binding_); binding_->host.spawn_effect(binding_->host.opaque, p0); people_back(binding_);
}
static void people_host_integrate_limited(void *opaque, fe2_object * p0, uint32_t p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.integrate_limited) return;
    people_out(binding_); binding_->host.integrate_limited(binding_->host.opaque, p0, p1); people_back(binding_);
}
static uint16_t people_host_relative_motion(void *opaque, fe2_object * p0, fe2_call_args * p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.relative_motion) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.relative_motion(binding_->host.opaque, p0, p1); people_back(binding_);
    return result_;
}
static uint16_t people_host_relative_probe(void *opaque, fe2_object * p0, fe2_call_args * p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.relative_probe) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.relative_probe(binding_->host.opaque, p0, p1); people_back(binding_);
    return result_;
}
static fe2_people_metric people_host_scaled_difference(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    fe2_people_metric result_;
    if (!binding_->host.scaled_difference) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.scaled_difference(binding_->host.opaque, p0, p1); people_back(binding_);
    return result_;
}
static void people_host_difference(void *opaque, fe2_object * p0, const fe2_call_args * p1, uint32_t p2[3])
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.difference) return;
    people_out(binding_); binding_->host.difference(binding_->host.opaque, p0, p1, p2); people_back(binding_);
}
static void people_host_linked_force(void *opaque, fe2_object * p0, uint32_t p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.linked_force) return;
    people_out(binding_); binding_->host.linked_force(binding_->host.opaque, p0, p1); people_back(binding_);
}
static void people_host_rates_toward(void *opaque, fe2_object * p0, fe2_object * p1, const fe2_call_args * p2)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.rates_toward) return;
    people_out(binding_); binding_->host.rates_toward(binding_->host.opaque, p0, p1, p2); people_back(binding_);
}
static void people_host_select_frame(void *opaque, fe2_object * p0)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.select_frame) return;
    people_out(binding_); binding_->host.select_frame(binding_->host.opaque, p0); people_back(binding_);
}
static void people_host_clamp_controls(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.clamp_controls) return;
    people_out(binding_); binding_->host.clamp_controls(binding_->host.opaque, p0, p1); people_back(binding_);
}
static void people_host_turn(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.turn) return;
    people_out(binding_); binding_->host.turn(binding_->host.opaque, p0, p1); people_back(binding_);
}
static void people_host_apply_controls(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.apply_controls) return;
    people_out(binding_); binding_->host.apply_controls(binding_->host.opaque, p0, p1); people_back(binding_);
}
static void people_host_hold_controls(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.hold_controls) return;
    people_out(binding_); binding_->host.hold_controls(binding_->host.opaque, p0, p1); people_back(binding_);
}
static void people_host_steer_controls(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.steer_controls) return;
    people_out(binding_); binding_->host.steer_controls(binding_->host.opaque, p0, p1); people_back(binding_);
}
static uint32_t people_host_random(void *opaque, uint32_t * p0)
{
    fe2_session_people_overlay *binding_ = opaque;
    uint32_t result_;
    if (!binding_->host.random) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.random(binding_->host.opaque, p0); people_back(binding_);
    return result_;
}
static uint16_t people_host_random_below(void *opaque, uint16_t p0)
{
    fe2_session_people_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.random_below) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.random_below(binding_->host.opaque, p0); people_back(binding_);
    return result_;
}
static void people_host_basis(void *opaque, fe2_object * p0)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.basis) return;
    people_out(binding_); binding_->host.basis(binding_->host.opaque, p0); people_back(binding_);
}
static void people_host_transform_vector(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.transform_vector) return;
    people_out(binding_); binding_->host.transform_vector(binding_->host.opaque, p0, p1); people_back(binding_);
}
static void people_host_post_event(void *opaque, uint16_t p0, uint16_t p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.post_event) return;
    people_out(binding_); binding_->host.post_event(binding_->host.opaque, p0, p1); people_back(binding_);
}
static void people_host_play_effect(void *opaque, uint16_t p0)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.play_effect) return;
    people_out(binding_); binding_->host.play_effect(binding_->host.opaque, p0); people_back(binding_);
}
static void people_host_play_sound(void *opaque, uint16_t p0, uint8_t p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.play_sound) return;
    people_out(binding_); binding_->host.play_sound(binding_->host.opaque, p0, p1); people_back(binding_);
}
static void people_host_check_music(void *opaque)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.check_music) return;
    people_out(binding_); binding_->host.check_music(binding_->host.opaque); people_back(binding_);
}
static void people_host_select_music(void *opaque, uint16_t p0)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.select_music) return;
    people_out(binding_); binding_->host.select_music(binding_->host.opaque, p0); people_back(binding_);
}
static fe2_people_reply people_host_galactic_map_relationship(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    fe2_people_reply result_;
    if (!binding_->host.galactic_map_relationship) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.galactic_map_relationship(binding_->host.opaque, p0, p1); people_back(binding_);
    return result_;
}
static fe2_people_reply people_host_catalogue_query(void *opaque, uint8_t p0, const fe2_call_args * p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    fe2_people_reply result_;
    if (!binding_->host.catalogue_query) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.catalogue_query(binding_->host.opaque, p0, p1); people_back(binding_);
    return result_;
}
static fe2_people_reply people_host_market_service(void *opaque, uint16_t p0, uint8_t p1, const fe2_call_args * p2)
{
    fe2_session_people_overlay *binding_ = opaque;
    fe2_people_reply result_;
    if (!binding_->host.market_overlay_service) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.market_overlay_service(binding_->host.opaque, p0, p1, p2); people_back(binding_);
    return result_;
}
static void people_host_names_name(void *opaque, uint16_t p0, uint32_t p1, char p2[FE2_PEOPLE_NAME_BYTES])
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.names_name) return;
    people_out(binding_); binding_->host.names_name(binding_->host.opaque, p0, p1, p2); people_back(binding_);
}
static uint8_t people_host_commander_check(void *opaque)
{
    fe2_session_people_overlay *binding_ = opaque;
    uint8_t result_;
    if (!binding_->host.commander_check) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.commander_check(binding_->host.opaque); people_back(binding_);
    return result_;
}
static void people_host_station_message(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.station_message) return;
    people_out(binding_); binding_->host.station_message(binding_->host.opaque, p0, p1); people_back(binding_);
}
static void people_host_offences_event(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_people_overlay *binding_ = opaque;
    if (!binding_->host.offences_event) return;
    people_out(binding_); binding_->host.offences_event(binding_->host.opaque, p0, p1); people_back(binding_);
}
static fe2_people_reply people_host_hyperspace_arrival(void *opaque, const fe2_call_args * p0)
{
    fe2_session_people_overlay *binding_ = opaque;
    fe2_people_reply result_;
    if (!binding_->host.hyperspace_arrival) {
        people_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    people_out(binding_); result_ = binding_->host.hyperspace_arrival(binding_->host.opaque, p0); people_back(binding_);
    return result_;
}
static void people_forward_all(fe2_people_services *v)
{
    v->visit = people_host_visit;
    v->lookup = people_host_lookup;
    v->allocate = people_host_allocate;
    v->allocate_upper = people_host_allocate_upper;
    v->release = people_host_release;
    v->remove = people_host_remove;
    v->attach = people_host_attach;
    v->set_slot_flags = people_host_set_slot_flags;
    v->setup = people_host_setup;
    v->model = people_host_model;
    v->scan_contacts = people_host_scan_contacts;
    v->damage = people_host_damage;
    v->activate_effect = people_host_activate_effect;
    v->consume_effect = people_host_consume_effect;
    v->capacity_used = people_host_capacity_used;
    v->activate_slot = people_host_activate_slot;
    v->deploy_slot = people_host_deploy_slot;
    v->spawn_effect = people_host_spawn_effect;
    v->integrate_limited = people_host_integrate_limited;
    v->relative_motion = people_host_relative_motion;
    v->relative_probe = people_host_relative_probe;
    v->scaled_difference = people_host_scaled_difference;
    v->difference = people_host_difference;
    v->linked_force = people_host_linked_force;
    v->rates_toward = people_host_rates_toward;
    v->select_frame = people_host_select_frame;
    v->clamp_controls = people_host_clamp_controls;
    v->turn = people_host_turn;
    v->apply_controls = people_host_apply_controls;
    v->hold_controls = people_host_hold_controls;
    v->steer_controls = people_host_steer_controls;
    v->random = people_host_random;
    v->random_below = people_host_random_below;
    v->basis = people_host_basis;
    v->transform_vector = people_host_transform_vector;
    v->post_event = people_host_post_event;
    v->play_effect = people_host_play_effect;
    v->play_sound = people_host_play_sound;
    v->check_music = people_host_check_music;
    v->select_music = people_host_select_music;
    v->galactic_map_relationship = people_host_galactic_map_relationship;
    v->catalogue_query = people_host_catalogue_query;
    v->market_overlay_service = people_host_market_service;
    v->names_name = people_host_names_name;
    v->commander_check = people_host_commander_check;
    v->station_message = people_host_station_message;
    v->offences_event = people_host_offences_event;
    v->hyperspace_arrival = people_host_hyperspace_arrival;
}
