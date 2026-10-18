/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
static void flight_host_visit_objects(void *opaque, fe2_object_list * p0, fe2_flight_visitor p1, void * p2)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.visit_objects) return;
    flight_out(binding_); binding_->host.visit_objects(binding_->host.opaque, p0, p1, p2); flight_back(binding_);
}
static void flight_host_visit_projected(void *opaque, fe2_object_list * p0, fe2_flight_screen_visitor p1, void * p2)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.visit_projected) return;
    flight_out(binding_); binding_->host.visit_projected(binding_->host.opaque, p0, p1, p2); flight_back(binding_);
}
static fe2_object * flight_host_lookup(void *opaque, uint8_t p0, const fe2_call_args * p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    fe2_object * result_;
    if (!binding_->host.lookup) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.lookup(binding_->host.opaque, p0, p1); flight_back(binding_);
    return result_;
}
static uint8_t flight_host_handle_flags(void *opaque, const fe2_object_list * p0, uint8_t p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    uint8_t result_;
    if (!binding_->host.handle_flags) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.handle_flags(binding_->host.opaque, p0, p1); flight_back(binding_);
    return result_;
}
static uint16_t flight_host_stray_word(void *opaque, const fe2_object * p0, uint8_t p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.stray_word) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.stray_word(binding_->host.opaque, p0, p1); flight_back(binding_);
    return result_;
}
static fe2_object * flight_host_allocate(void *opaque, const fe2_call_args * p0, uint16_t * p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    fe2_object * result_;
    if (!binding_->host.allocate) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.allocate(binding_->host.opaque, p0, p1); flight_back(binding_);
    return result_;
}
static fe2_object * flight_host_allocate_upper(void *opaque, const fe2_call_args * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    fe2_object * result_;
    if (!binding_->host.allocate_upper) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.allocate_upper(binding_->host.opaque, p0); flight_back(binding_);
    return result_;
}
static void flight_host_release(void *opaque, fe2_object * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.release) return;
    flight_out(binding_); binding_->host.release(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_set_slot_flags(void *opaque, fe2_object * p0, uint8_t p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.set_slot_flags) return;
    flight_out(binding_); binding_->host.set_slot_flags(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static const fe2_flight_model * flight_host_model(void *opaque, uint16_t p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    const fe2_flight_model * result_;
    if (!binding_->host.model) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.model(binding_->host.opaque, p0); flight_back(binding_);
    return result_;
}
static fe2_flight_contacts flight_host_scan_contacts(void *opaque, fe2_object * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    fe2_flight_contacts result_;
    if (!binding_->host.scan_contacts) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.scan_contacts(binding_->host.opaque, p0); flight_back(binding_);
    return result_;
}
static void flight_host_refresh_contacts(void *opaque, fe2_object * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.refresh_contacts) return;
    flight_out(binding_); binding_->host.refresh_contacts(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_damage(void *opaque, fe2_object * p0, uint16_t p1, const fe2_call_args * p2)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.damage) return;
    flight_out(binding_); binding_->host.damage(binding_->host.opaque, p0, p1, p2); flight_back(binding_);
}
static void flight_host_activate_effect(void *opaque, fe2_object * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.activate_effect) return;
    flight_out(binding_); binding_->host.activate_effect(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_consume_effect(void *opaque, fe2_object * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.consume_effect) return;
    flight_out(binding_); binding_->host.consume_effect(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_update_capacity(void *opaque, fe2_object * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.update_capacity) return;
    flight_out(binding_); binding_->host.update_capacity(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_activate_slot(void *opaque, fe2_object * p0, uint8_t p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.activate_slot) return;
    flight_out(binding_); binding_->host.activate_slot(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_host_deploy_slot(void *opaque, fe2_object * p0, uint16_t p1, uint8_t p2, const fe2_call_args * p3)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.deploy_slot) return;
    flight_out(binding_); binding_->host.deploy_slot(binding_->host.opaque, p0, p1, p2, p3); flight_back(binding_);
}
static void flight_host_advance_failures(void *opaque, fe2_object * p0, uint32_t p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.advance_failures) return;
    flight_out(binding_); binding_->host.advance_failures(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_host_spawn_effect(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.spawn_effect) return;
    flight_out(binding_); binding_->host.spawn_effect(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static int flight_host_probe(void *opaque, const fe2_call_args * p0, fe2_flight_probe * p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.probe) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.probe(binding_->host.opaque, p0, p1); flight_back(binding_);
    return result_;
}
static void flight_host_rebuild_secondary_basis(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.rebuild_secondary_basis) return;
    flight_out(binding_); binding_->host.rebuild_secondary_basis(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_host_integrate_limited(void *opaque, fe2_object * p0, uint32_t p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.integrate_limited) return;
    flight_out(binding_); binding_->host.integrate_limited(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_host_integrate(void *opaque, fe2_object * p0, uint32_t p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.integrate) return;
    flight_out(binding_); binding_->host.integrate(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static fe2_flight_feedback flight_host_axis_feedback(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    fe2_flight_feedback result_;
    if (!binding_->host.axis_feedback) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.axis_feedback(binding_->host.opaque, p0, p1); flight_back(binding_);
    return result_;
}
static fe2_flight_scaled flight_host_scaled_difference(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    fe2_flight_scaled result_;
    if (!binding_->host.scaled_difference) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.scaled_difference(binding_->host.opaque, p0, p1); flight_back(binding_);
    return result_;
}
static void flight_host_difference(void *opaque, fe2_object * p0, const fe2_call_args * p1, int32_t p2[3])
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.difference) return;
    flight_out(binding_); binding_->host.difference(binding_->host.opaque, p0, p1, p2); flight_back(binding_);
}
static void flight_host_linked_force(void *opaque, fe2_object * p0, uint32_t p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.linked_force) return;
    flight_out(binding_); binding_->host.linked_force(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_host_radial_rate(void *opaque, fe2_object * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.radial_rate) return;
    flight_out(binding_); binding_->host.radial_rate(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_normalize(void *opaque, const fe2_call_args * p0, int16_t p1[3])
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.normalize) return;
    flight_out(binding_); binding_->host.normalize(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_host_rates_toward(void *opaque, fe2_object * p0, fe2_object * p1, const fe2_call_args * p2)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.rates_toward) return;
    flight_out(binding_); binding_->host.rates_toward(binding_->host.opaque, p0, p1, p2); flight_back(binding_);
}
static void flight_host_select_frame(void *opaque, fe2_object * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.select_frame) return;
    flight_out(binding_); binding_->host.select_frame(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_clamp_controls(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.clamp_controls) return;
    flight_out(binding_); binding_->host.clamp_controls(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_host_turn(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.turn) return;
    flight_out(binding_); binding_->host.turn(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_host_rebuild_basis(void *opaque, fe2_object * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.rebuild_basis) return;
    flight_out(binding_); binding_->host.rebuild_basis(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_transform_feedback(void *opaque, fe2_object * p0, const fe2_call_args * p1, int16_t p2[3])
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.transform_feedback) return;
    flight_out(binding_); binding_->host.transform_feedback(binding_->host.opaque, p0, p1, p2); flight_back(binding_);
}
static void flight_host_set_panel_mode(void *opaque, uint16_t p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.set_panel_mode) return;
    flight_out(binding_); binding_->host.set_panel_mode(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_show_panel(void *opaque)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.show_panel) return;
    flight_out(binding_); binding_->host.show_panel(binding_->host.opaque); flight_back(binding_);
}
static int flight_host_poll_key(void *opaque, uint8_t * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.poll_key) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.poll_key(binding_->host.opaque, p0); flight_back(binding_);
    return result_;
}
static void flight_host_forward_key(void *opaque, uint8_t p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.forward_key) return;
    flight_out(binding_); binding_->host.forward_key(binding_->host.opaque, p0); flight_back(binding_);
}
static uint16_t flight_host_pointer(void *opaque, uint16_t * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.pointer) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.pointer(binding_->host.opaque, p0); flight_back(binding_);
    return result_;
}
static void flight_host_set_pointer(void *opaque, uint16_t p0, uint16_t p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.set_pointer) return;
    flight_out(binding_); binding_->host.set_pointer(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_host_set_controls(void *opaque, uint16_t p0, uint8_t p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.set_controls) return;
    flight_out(binding_); binding_->host.set_controls(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_host_set_widgets(void *opaque, const uint16_t * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.set_widgets) return;
    flight_out(binding_); binding_->host.set_widgets(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_add_region(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2, uint16_t p3, uint16_t * p4, uint16_t * p5)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.add_region) return;
    flight_out(binding_); binding_->host.add_region(binding_->host.opaque, p0, p1, p2, p3, p4, p5); flight_back(binding_);
}
static void flight_host_remove_region(void *opaque, uint16_t p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.remove_region) return;
    flight_out(binding_); binding_->host.remove_region(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_clear_regions(void *opaque)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.clear_regions) return;
    flight_out(binding_); binding_->host.clear_regions(binding_->host.opaque); flight_back(binding_);
}
static void flight_host_draw_clipped_line(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2, uint16_t p3)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.draw_clipped_line) return;
    flight_out(binding_); binding_->host.draw_clipped_line(binding_->host.opaque, p0, p1, p2, p3); flight_back(binding_);
}
static void flight_host_draw_line(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2, uint16_t p3)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.draw_line) return;
    flight_out(binding_); binding_->host.draw_line(binding_->host.opaque, p0, p1, p2, p3); flight_back(binding_);
}
static void flight_host_draw_sprite(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.draw_sprite) return;
    flight_out(binding_); binding_->host.draw_sprite(binding_->host.opaque, p0, p1, p2); flight_back(binding_);
}
static void flight_host_draw_name(void *opaque, const fe2_object * p0, uint8_t p1, uint16_t p2, uint16_t p3)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.draw_name) return;
    flight_out(binding_); binding_->host.draw_name(binding_->host.opaque, p0, p1, p2, p3); flight_back(binding_);
}
static void flight_host_draw_text(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2, uint16_t p3, const fe2_call_args * p4)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.draw_text) return;
    flight_out(binding_); binding_->host.draw_text(binding_->host.opaque, p0, p1, p2, p3, p4); flight_back(binding_);
}
static void flight_host_draw_text_both(void *opaque, uint16_t p0, uint8_t p1, uint16_t p2, uint16_t p3, const fe2_call_args * p4)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.draw_text_both) return;
    flight_out(binding_); binding_->host.draw_text_both(binding_->host.opaque, p0, p1, p2, p3, p4); flight_back(binding_);
}
static void flight_host_draw_text_shadowed(void *opaque, uint16_t p0, uint8_t p1, uint16_t p2, uint16_t p3, const fe2_call_args * p4)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.draw_text_shadowed) return;
    flight_out(binding_); binding_->host.draw_text_shadowed(binding_->host.opaque, p0, p1, p2, p3, p4); flight_back(binding_);
}
static void flight_host_format_table_text(void *opaque, uint16_t p0, const fe2_text_table * p1, fe2_text_out * p2)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.format_table_text) return;
    flight_out(binding_); binding_->host.format_table_text(binding_->host.opaque, p0, p1, p2); flight_back(binding_);
}
static uint16_t flight_host_palette_index(void *opaque, uint8_t p0, uint16_t p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.palette_index) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.palette_index(binding_->host.opaque, p0, p1); flight_back(binding_);
    return result_;
}
static uint16_t flight_host_palette_rgb(void *opaque, uint16_t p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.palette_rgb) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.palette_rgb(binding_->host.opaque, p0); flight_back(binding_);
    return result_;
}
static void flight_host_clear_drawing(void *opaque)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.clear_drawing) return;
    flight_out(binding_); binding_->host.clear_drawing(binding_->host.opaque); flight_back(binding_);
}
static void flight_host_present(void *opaque)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.present) return;
    flight_out(binding_); binding_->host.present(binding_->host.opaque); flight_back(binding_);
}
static void flight_host_render_scene(void *opaque, const fe2_call_args * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.render_scene) return;
    flight_out(binding_); binding_->host.render_scene(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_render_frame_model(void *opaque, fe2_object * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.render_frame_model) return;
    flight_out(binding_); binding_->host.render_frame_model(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_render_object(void *opaque, fe2_object * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.render_object) return;
    flight_out(binding_); binding_->host.render_object(binding_->host.opaque, p0); flight_back(binding_);
}
static uint32_t flight_host_random(void *opaque, uint32_t * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    uint32_t result_;
    if (!binding_->host.random) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.random(binding_->host.opaque, p0); flight_back(binding_);
    return result_;
}
static fe2_scalar flight_host_normalize_unsigned(void *opaque, fe2_scalar p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    fe2_scalar result_;
    if (!binding_->host.normalize_unsigned) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.normalize_unsigned(binding_->host.opaque, p0); flight_back(binding_);
    return result_;
}
static void flight_host_basis(void *opaque, fe2_object * p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.basis) return;
    flight_out(binding_); binding_->host.basis(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_move_object(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.move_object) return;
    flight_out(binding_); binding_->host.move_object(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_host_transform_vector(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.transform_vector) return;
    flight_out(binding_); binding_->host.transform_vector(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_host_project(void *opaque, int32_t p0, int32_t p1, int32_t p2, uint16_t * p3, uint16_t * p4)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.project) return;
    flight_out(binding_); binding_->host.project(binding_->host.opaque, p0, p1, p2, p3, p4); flight_back(binding_);
}
static void flight_host_rotate_pair(void *opaque, fe2_object * p0, unsigned p1, unsigned p2, const fe2_call_args * p3)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.rotate_pair) return;
    flight_out(binding_); binding_->host.rotate_pair(binding_->host.opaque, p0, p1, p2, p3); flight_back(binding_);
}
static uint16_t flight_host_vector_length(void *opaque, int16_t p0, int16_t p1, int16_t p2)
{
    fe2_session_flight_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.vector_length) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.vector_length(binding_->host.opaque, p0, p1, p2); flight_back(binding_);
    return result_;
}
static void flight_host_post_event(void *opaque, uint16_t p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.post_event) return;
    flight_out(binding_); binding_->host.post_event(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_post_event_value(void *opaque, uint16_t p0, uint16_t p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.post_event_value) return;
    flight_out(binding_); binding_->host.post_event_value(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_host_play_effect(void *opaque, uint16_t p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.play_effect) return;
    flight_out(binding_); binding_->host.play_effect(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_play_duration(void *opaque, uint16_t p0, uint16_t p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.play_duration) return;
    flight_out(binding_); binding_->host.play_duration(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_host_ambient(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.ambient) return;
    flight_out(binding_); binding_->host.ambient(binding_->host.opaque, p0, p1, p2); flight_back(binding_);
}
static void flight_host_check_music(void *opaque)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.check_music) return;
    flight_out(binding_); binding_->host.check_music(binding_->host.opaque); flight_back(binding_);
}
static void flight_host_select_music(void *opaque, uint16_t p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.select_music) return;
    flight_out(binding_); binding_->host.select_music(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_fatal_error(void *opaque, uint16_t p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.fatal_error) return;
    flight_out(binding_); binding_->host.fatal_error(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_restart_session(void *opaque, uint16_t p0)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.restart_session) return;
    flight_out(binding_); binding_->host.restart_session(binding_->host.opaque, p0); flight_back(binding_);
}
static void flight_host_panels_panel(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.panels_panel) return;
    flight_out(binding_); binding_->host.panels_panel(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_host_panels_widgets(void *opaque, uint16_t p0, uint16_t p1, const fe2_call_args * p2)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.panels_widgets) return;
    flight_out(binding_); binding_->host.panels_widgets(binding_->host.opaque, p0, p1, p2); flight_back(binding_);
}
static void flight_host_market_service(void *opaque, uint16_t p0, uint16_t p1, const fe2_call_args * p2, fe2_flight_market_reply * p3)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.market_overlay_service) return;
    flight_out(binding_); binding_->host.market_overlay_service(binding_->host.opaque, p0, p1, p2, p3); flight_back(binding_);
}
static void flight_host_names_name(void *opaque, uint16_t p0, uint32_t p1, char p2[FE2_FLIGHT_NAME_BYTES])
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.names_name) return;
    flight_out(binding_); binding_->host.names_name(binding_->host.opaque, p0, p1, p2); flight_back(binding_);
}
static void flight_host_commander_reset(void *opaque)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.commander_reset) return;
    flight_out(binding_); binding_->host.commander_reset(binding_->host.opaque); flight_back(binding_);
}
static fe2_object * flight_host_people_update(void *opaque, fe2_object * p0, uint32_t p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    fe2_object * result_;
    if (!binding_->host.people_update) {
        flight_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    flight_out(binding_); result_ = binding_->host.people_update(binding_->host.opaque, p0, p1); flight_back(binding_);
    return result_;
}
static void flight_host_station_message(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.station_message) return;
    flight_out(binding_); binding_->host.station_message(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_host_offences_event(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.offences_event) return;
    flight_out(binding_); binding_->host.offences_event(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_host_hyperspace_query(void *opaque, uint16_t p0, const fe2_call_args * p1, fe2_flight_drive_reply * p2)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.hyperspace_query) return;
    flight_out(binding_); binding_->host.hyperspace_query(binding_->host.opaque, p0, p1, p2); flight_back(binding_);
}
static void flight_host_hyperspace_jump(void *opaque)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.hyperspace_jump) return;
    flight_out(binding_); binding_->host.hyperspace_jump(binding_->host.opaque); flight_back(binding_);
}
static void flight_host_mining_command(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_flight_overlay *binding_ = opaque;
    if (!binding_->host.mining_command) return;
    flight_out(binding_); binding_->host.mining_command(binding_->host.opaque, p0, p1); flight_back(binding_);
}
static void flight_forward_all(fe2_flight_services *v)
{
    v->visit_objects = flight_host_visit_objects;
    v->visit_projected = flight_host_visit_projected;
    v->lookup = flight_host_lookup;
    v->handle_flags = flight_host_handle_flags;
    v->stray_word = flight_host_stray_word;
    v->allocate = flight_host_allocate;
    v->allocate_upper = flight_host_allocate_upper;
    v->release = flight_host_release;
    v->set_slot_flags = flight_host_set_slot_flags;
    v->model = flight_host_model;
    v->scan_contacts = flight_host_scan_contacts;
    v->refresh_contacts = flight_host_refresh_contacts;
    v->damage = flight_host_damage;
    v->activate_effect = flight_host_activate_effect;
    v->consume_effect = flight_host_consume_effect;
    v->update_capacity = flight_host_update_capacity;
    v->activate_slot = flight_host_activate_slot;
    v->deploy_slot = flight_host_deploy_slot;
    v->advance_failures = flight_host_advance_failures;
    v->spawn_effect = flight_host_spawn_effect;
    v->probe = flight_host_probe;
    v->rebuild_secondary_basis = flight_host_rebuild_secondary_basis;
    v->integrate_limited = flight_host_integrate_limited;
    v->integrate = flight_host_integrate;
    v->axis_feedback = flight_host_axis_feedback;
    v->scaled_difference = flight_host_scaled_difference;
    v->difference = flight_host_difference;
    v->linked_force = flight_host_linked_force;
    v->radial_rate = flight_host_radial_rate;
    v->normalize = flight_host_normalize;
    v->rates_toward = flight_host_rates_toward;
    v->select_frame = flight_host_select_frame;
    v->clamp_controls = flight_host_clamp_controls;
    v->turn = flight_host_turn;
    v->rebuild_basis = flight_host_rebuild_basis;
    v->transform_feedback = flight_host_transform_feedback;
    v->set_panel_mode = flight_host_set_panel_mode;
    v->show_panel = flight_host_show_panel;
    v->poll_key = flight_host_poll_key;
    v->forward_key = flight_host_forward_key;
    v->pointer = flight_host_pointer;
    v->set_pointer = flight_host_set_pointer;
    v->set_controls = flight_host_set_controls;
    v->set_widgets = flight_host_set_widgets;
    v->add_region = flight_host_add_region;
    v->remove_region = flight_host_remove_region;
    v->clear_regions = flight_host_clear_regions;
    v->draw_clipped_line = flight_host_draw_clipped_line;
    v->draw_line = flight_host_draw_line;
    v->draw_sprite = flight_host_draw_sprite;
    v->draw_name = flight_host_draw_name;
    v->draw_text = flight_host_draw_text;
    v->draw_text_both = flight_host_draw_text_both;
    v->draw_text_shadowed = flight_host_draw_text_shadowed;
    v->format_table_text = flight_host_format_table_text;
    v->palette_index = flight_host_palette_index;
    v->palette_rgb = flight_host_palette_rgb;
    v->clear_drawing = flight_host_clear_drawing;
    v->present = flight_host_present;
    v->render_scene = flight_host_render_scene;
    v->render_frame_model = flight_host_render_frame_model;
    v->render_object = flight_host_render_object;
    v->random = flight_host_random;
    v->normalize_unsigned = flight_host_normalize_unsigned;
    v->basis = flight_host_basis;
    v->move_object = flight_host_move_object;
    v->transform_vector = flight_host_transform_vector;
    v->project = flight_host_project;
    v->rotate_pair = flight_host_rotate_pair;
    v->vector_length = flight_host_vector_length;
    v->post_event = flight_host_post_event;
    v->post_event_value = flight_host_post_event_value;
    v->play_effect = flight_host_play_effect;
    v->play_duration = flight_host_play_duration;
    v->ambient = flight_host_ambient;
    v->check_music = flight_host_check_music;
    v->select_music = flight_host_select_music;
    v->fatal_error = flight_host_fatal_error;
    v->restart_session = flight_host_restart_session;
    v->panels_panel = flight_host_panels_panel;
    v->panels_widgets = flight_host_panels_widgets;
    v->market_overlay_service = flight_host_market_service;
    v->names_name = flight_host_names_name;
    v->commander_reset = flight_host_commander_reset;
    v->people_update = flight_host_people_update;
    v->station_message = flight_host_station_message;
    v->offences_event = flight_host_offences_event;
    v->hyperspace_query = flight_host_hyperspace_query;
    v->hyperspace_jump = flight_host_hyperspace_jump;
    v->mining_command = flight_host_mining_command;
}
