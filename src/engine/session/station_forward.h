/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
static fe2_object * station_host_object(void *opaque, fe2_ref p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    fe2_object * result_;
    if (!binding_->host.object) {
        station_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    station_out(binding_); result_ = binding_->host.object(binding_->host.opaque, p0); station_back(binding_);
    return result_;
}
static fe2_ref station_host_lookup(void *opaque, uint8_t p0, const fe2_call_args * p1)
{
    fe2_session_station_overlay *binding_ = opaque;
    fe2_ref result_;
    if (!binding_->host.lookup) {
        station_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    station_out(binding_); result_ = binding_->host.lookup(binding_->host.opaque, p0, p1); station_back(binding_);
    return result_;
}
static const fe2_station_model * station_host_model(void *opaque, uint16_t p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    const fe2_station_model * result_;
    if (!binding_->host.model) {
        station_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    station_out(binding_); result_ = binding_->host.model(binding_->host.opaque, p0); station_back(binding_);
    return result_;
}
static void station_host_visit_objects(void *opaque, fe2_ref p0, fe2_station_visitor p1, void * p2, const fe2_call_args * p3)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.visit_objects) return;
    station_out(binding_); binding_->host.visit_objects(binding_->host.opaque, p0, p1, p2, p3); station_back(binding_);
}
static void station_host_prepare_object(void *opaque, const fe2_call_args * p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.prepare_object) return;
    station_out(binding_); binding_->host.prepare_object(binding_->host.opaque, p0); station_back(binding_);
}
static void station_host_clear_objects(void *opaque, const fe2_call_args * p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.clear_objects) return;
    station_out(binding_); binding_->host.clear_objects(binding_->host.opaque, p0); station_back(binding_);
}
static void station_host_attach_object(void *opaque, const fe2_call_args * p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.attach_object) return;
    station_out(binding_); binding_->host.attach_object(binding_->host.opaque, p0); station_back(binding_);
}
static void station_host_set_slot_flags(void *opaque, fe2_ref p0, uint8_t p1)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.set_slot_flags) return;
    station_out(binding_); binding_->host.set_slot_flags(binding_->host.opaque, p0, p1); station_back(binding_);
}
static void station_host_move_object(void *opaque, const fe2_call_args * p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.move_object) return;
    station_out(binding_); binding_->host.move_object(binding_->host.opaque, p0); station_back(binding_);
}
static void station_host_orient_object(void *opaque, fe2_ref p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.orient_object) return;
    station_out(binding_); binding_->host.orient_object(binding_->host.opaque, p0); station_back(binding_);
}
static void station_host_render_object(void *opaque, fe2_ref p0, const fe2_call_args * p1)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.render_object) return;
    station_out(binding_); binding_->host.render_object(binding_->host.opaque, p0, p1); station_back(binding_);
}
static fe2_ref station_host_work_record(void *opaque, enum fe2_station_work p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    fe2_ref result_;
    if (!binding_->host.work_record) {
        station_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    station_out(binding_); result_ = binding_->host.work_record(binding_->host.opaque, p0); station_back(binding_);
    return result_;
}
static void station_host_select_controls(void *opaque, uint16_t p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.select_controls) return;
    station_out(binding_); binding_->host.select_controls(binding_->host.opaque, p0); station_back(binding_);
}
static void station_host_set_widgets(void *opaque, const uint16_t * p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.set_widgets) return;
    station_out(binding_); binding_->host.set_widgets(binding_->host.opaque, p0); station_back(binding_);
}
static void station_host_show_panel(void *opaque)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.show_panel) return;
    station_out(binding_); binding_->host.show_panel(binding_->host.opaque); station_back(binding_);
}
static void station_host_clear_regions(void *opaque)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.clear_regions) return;
    station_out(binding_); binding_->host.clear_regions(binding_->host.opaque); station_back(binding_);
}
static void station_host_restore_view(void *opaque)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.restore_view) return;
    station_out(binding_); binding_->host.restore_view(binding_->host.opaque); station_back(binding_);
}
static void station_host_open_view(void *opaque)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.open_view) return;
    station_out(binding_); binding_->host.open_view(binding_->host.opaque); station_back(binding_);
}
static void station_host_clear_drawing(void *opaque)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.clear_drawing) return;
    station_out(binding_); binding_->host.clear_drawing(binding_->host.opaque); station_back(binding_);
}
static void station_host_set_controls(void *opaque, uint16_t p0, uint16_t p1, uint8_t p2)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.set_controls) return;
    station_out(binding_); binding_->host.set_controls(binding_->host.opaque, p0, p1, p2); station_back(binding_);
}
static void station_host_set_view(void *opaque, uint16_t p0, uint8_t p1)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.set_view) return;
    station_out(binding_); binding_->host.set_view(binding_->host.opaque, p0, p1); station_back(binding_);
}
static void station_host_clear_panel(void *opaque)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.clear_panel) return;
    station_out(binding_); binding_->host.clear_panel(binding_->host.opaque); station_back(binding_);
}
static void station_host_clear_picture(void *opaque)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.clear_picture) return;
    station_out(binding_); binding_->host.clear_picture(binding_->host.opaque); station_back(binding_);
}
static void station_host_draw_frame(void *opaque, uint16_t p0, uint16_t p1, uint8_t p2, uint8_t p3, uint16_t p4)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.draw_frame) return;
    station_out(binding_); binding_->host.draw_frame(binding_->host.opaque, p0, p1, p2, p3, p4); station_back(binding_);
}
static void station_host_draw_line(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2, uint16_t p3)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.draw_line) return;
    station_out(binding_); binding_->host.draw_line(binding_->host.opaque, p0, p1, p2, p3); station_back(binding_);
}
static void station_host_draw_icon(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.draw_icon) return;
    station_out(binding_); binding_->host.draw_icon(binding_->host.opaque, p0, p1, p2); station_back(binding_);
}
static void station_host_draw_sprite(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2, const fe2_call_args * p3)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.draw_sprite) return;
    station_out(binding_); binding_->host.draw_sprite(binding_->host.opaque, p0, p1, p2, p3); station_back(binding_);
}
static void station_host_present(void *opaque)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.present) return;
    station_out(binding_); binding_->host.present(binding_->host.opaque); station_back(binding_);
}
static void station_host_draw_text_shadowed(void *opaque, uint16_t p0, uint8_t p1, uint16_t p2, uint16_t p3, const fe2_call_args * p4)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.draw_text_shadowed) return;
    station_out(binding_); binding_->host.draw_text_shadowed(binding_->host.opaque, p0, p1, p2, p3, p4); station_back(binding_);
}
static void station_host_draw_text_both(void *opaque, uint16_t p0, uint8_t p1, uint16_t p2, uint16_t p3, const fe2_call_args * p4)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.draw_text_both) return;
    station_out(binding_); binding_->host.draw_text_both(binding_->host.opaque, p0, p1, p2, p3, p4); station_back(binding_);
}
static void station_host_draw_text(void *opaque, uint16_t p0, uint8_t p1, uint16_t p2, uint16_t p3, const fe2_call_args * p4)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.draw_text) return;
    station_out(binding_); binding_->host.draw_text(binding_->host.opaque, p0, p1, p2, p3, p4); station_back(binding_);
}
static int station_host_poll_key(void *opaque, uint16_t * p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.poll_key) {
        station_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    station_out(binding_); result_ = binding_->host.poll_key(binding_->host.opaque, p0); station_back(binding_);
    return result_;
}
static void station_host_forward_key(void *opaque, uint16_t p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.forward_key) return;
    station_out(binding_); binding_->host.forward_key(binding_->host.opaque, p0); station_back(binding_);
}
static void station_host_unregister_key(void *opaque, uint16_t p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.unregister_key) return;
    station_out(binding_); binding_->host.unregister_key(binding_->host.opaque, p0); station_back(binding_);
}
static void station_host_post_event(void *opaque, uint16_t p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.post_event) return;
    station_out(binding_); binding_->host.post_event(binding_->host.opaque, p0); station_back(binding_);
}
static void station_host_stop_time(void *opaque)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.stop_time) return;
    station_out(binding_); binding_->host.stop_time(binding_->host.opaque); station_back(binding_);
}
static uint16_t station_host_random_below(void *opaque, uint16_t p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.random_below) {
        station_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    station_out(binding_); result_ = binding_->host.random_below(binding_->host.opaque, p0); station_back(binding_);
    return result_;
}
static fe2_station_sample station_host_bounded_sample(void *opaque, uint16_t p0, uint32_t p1, uint32_t p2)
{
    fe2_session_station_overlay *binding_ = opaque;
    fe2_station_sample result_;
    if (!binding_->host.bounded_sample) {
        station_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    station_out(binding_); result_ = binding_->host.bounded_sample(binding_->host.opaque, p0, p1, p2); station_back(binding_);
    return result_;
}
static void station_host_play_effect(void *opaque, uint16_t p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.play_effect) return;
    station_out(binding_); binding_->host.play_effect(binding_->host.opaque, p0); station_back(binding_);
}
static void station_host_restart_session(void *opaque, uint16_t p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.restart_session) return;
    station_out(binding_); binding_->host.restart_session(binding_->host.opaque, p0); station_back(binding_);
}
static void station_host_systems_system(void *opaque, uint32_t * p0, uint32_t * p1, const fe2_call_args * p2)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.systems_system) return;
    station_out(binding_); binding_->host.systems_system(binding_->host.opaque, p0, p1, p2); station_back(binding_);
}
static uint16_t station_host_galactic_map_system(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_station_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.galactic_map_system) {
        station_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    station_out(binding_); result_ = binding_->host.galactic_map_system(binding_->host.opaque, p0, p1); station_back(binding_);
    return result_;
}
static void station_host_flight_activate_view(void *opaque)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.flight_activate_view) return;
    station_out(binding_); binding_->host.flight_activate_view(binding_->host.opaque); station_back(binding_);
}
static void station_host_flight_reset_capacity(void *opaque)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.flight_restore_capacity) return;
    station_out(binding_); binding_->host.flight_restore_capacity(binding_->host.opaque); station_back(binding_);
}
static void station_host_panels_panel(void *opaque, uint16_t p0, uint16_t p1, const fe2_call_args * p2)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.panels_panel) return;
    station_out(binding_); binding_->host.panels_panel(binding_->host.opaque, p0, p1, p2); station_back(binding_);
}
static fe2_station_market_reply station_host_market_market(void *opaque, uint16_t p0, uint8_t p1, const fe2_call_args * p2)
{
    fe2_session_station_overlay *binding_ = opaque;
    fe2_station_market_reply result_;
    if (!binding_->host.market_market) {
        station_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    station_out(binding_); result_ = binding_->host.market_market(binding_->host.opaque, p0, p1, p2); station_back(binding_);
    return result_;
}
static void station_host_names_name(void *opaque, uint16_t p0, uint32_t p1, enum fe2_station_work p2, fe2_station_name * p3)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.names_name) return;
    station_out(binding_); binding_->host.names_name(binding_->host.opaque, p0, p1, p2, p3); station_back(binding_);
}
static void station_host_commander_restore_time(void *opaque)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.commander_restore_time) return;
    station_out(binding_); binding_->host.commander_restore_time(binding_->host.opaque); station_back(binding_);
}
static void station_host_composite_portrait(void *opaque, uint32_t p0, const fe2_call_args * p1)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.composite_portrait) return;
    station_out(binding_); binding_->host.composite_portrait(binding_->host.opaque, p0, p1); station_back(binding_);
}
static void station_host_status_contract(void *opaque, const fe2_call_args * p0)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.status_contract) return;
    station_out(binding_); binding_->host.status_contract(binding_->host.opaque, p0); station_back(binding_);
}
static void station_host_offences_police(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.offences_police) return;
    station_out(binding_); binding_->host.offences_police(binding_->host.opaque, p0, p1); station_back(binding_);
}
static void station_host_hyperspace_ship(void *opaque, uint16_t p0, uint16_t p1, const fe2_call_args * p2)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.hyperspace_ship) return;
    station_out(binding_); binding_->host.hyperspace_ship(binding_->host.opaque, p0, p1, p2); station_back(binding_);
}
static uint16_t station_host_mining_service(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_station_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.mining_service) {
        station_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    station_out(binding_); result_ = binding_->host.mining_service(binding_->host.opaque, p0, p1); station_back(binding_);
    return result_;
}
static void station_host_interaction_text_text(void *opaque, uint16_t p0, fe2_text_out * p1)
{
    fe2_session_station_overlay *binding_ = opaque;
    if (!binding_->host.interaction_text_text) return;
    station_out(binding_); binding_->host.interaction_text_text(binding_->host.opaque, p0, p1); station_back(binding_);
}
static void station_forward_all(fe2_station_services *v)
{
    v->object = station_host_object;
    v->lookup = station_host_lookup;
    v->model = station_host_model;
    v->visit_objects = station_host_visit_objects;
    v->prepare_object = station_host_prepare_object;
    v->clear_objects = station_host_clear_objects;
    v->attach_object = station_host_attach_object;
    v->set_slot_flags = station_host_set_slot_flags;
    v->move_object = station_host_move_object;
    v->orient_object = station_host_orient_object;
    v->render_object = station_host_render_object;
    v->work_record = station_host_work_record;
    v->select_controls = station_host_select_controls;
    v->set_widgets = station_host_set_widgets;
    v->show_panel = station_host_show_panel;
    v->clear_regions = station_host_clear_regions;
    v->restore_view = station_host_restore_view;
    v->open_view = station_host_open_view;
    v->clear_drawing = station_host_clear_drawing;
    v->set_controls = station_host_set_controls;
    v->set_view = station_host_set_view;
    v->clear_panel = station_host_clear_panel;
    v->clear_picture = station_host_clear_picture;
    v->draw_frame = station_host_draw_frame;
    v->draw_line = station_host_draw_line;
    v->draw_icon = station_host_draw_icon;
    v->draw_sprite = station_host_draw_sprite;
    v->present = station_host_present;
    v->draw_text_shadowed = station_host_draw_text_shadowed;
    v->draw_text_both = station_host_draw_text_both;
    v->draw_text = station_host_draw_text;
    v->poll_key = station_host_poll_key;
    v->forward_key = station_host_forward_key;
    v->unregister_key = station_host_unregister_key;
    v->post_event = station_host_post_event;
    v->stop_time = station_host_stop_time;
    v->random_below = station_host_random_below;
    v->bounded_sample = station_host_bounded_sample;
    v->play_effect = station_host_play_effect;
    v->restart_session = station_host_restart_session;
    v->systems_system = station_host_systems_system;
    v->galactic_map_system = station_host_galactic_map_system;
    v->flight_activate_view = station_host_flight_activate_view;
    v->flight_restore_capacity = station_host_flight_reset_capacity;
    v->panels_panel = station_host_panels_panel;
    v->market_market = station_host_market_market;
    v->names_name = station_host_names_name;
    v->commander_restore_time = station_host_commander_restore_time;
    v->composite_portrait = station_host_composite_portrait;
    v->status_contract = station_host_status_contract;
    v->offences_police = station_host_offences_police;
    v->hyperspace_ship = station_host_hyperspace_ship;
    v->mining_service = station_host_mining_service;
    v->interaction_text_text = station_host_interaction_text_text;
}
