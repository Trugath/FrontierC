/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
static void navigation_host_set_panel_mode(void *opaque, uint16_t p0)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.set_panel_mode) return;
    navigation_out(binding_); binding_->host.set_panel_mode(binding_->host.opaque, p0); navigation_back(binding_);
}
static int navigation_host_poll_key(void *opaque, uint8_t * p0)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.poll_key) {
        navigation_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    navigation_out(binding_); result_ = binding_->host.poll_key(binding_->host.opaque, p0); navigation_back(binding_);
    return result_;
}
static void navigation_host_forward_key(void *opaque, uint8_t p0)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.forward_key) return;
    navigation_out(binding_); binding_->host.forward_key(binding_->host.opaque, p0); navigation_back(binding_);
}
static void navigation_host_show_panel(void *opaque)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.show_panel) return;
    navigation_out(binding_); binding_->host.show_panel(binding_->host.opaque); navigation_back(binding_);
}
static void navigation_host_restore_view(void *opaque)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.restore_view) return;
    navigation_out(binding_); binding_->host.restore_view(binding_->host.opaque); navigation_back(binding_);
}
static void navigation_host_set_controls(void *opaque, uint16_t p0, uint8_t p1)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.set_controls) return;
    navigation_out(binding_); binding_->host.set_controls(binding_->host.opaque, p0, p1); navigation_back(binding_);
}
static void navigation_host_set_pointer(void *opaque, uint16_t p0, uint16_t p1)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.set_pointer) return;
    navigation_out(binding_); binding_->host.set_pointer(binding_->host.opaque, p0, p1); navigation_back(binding_);
}
static void navigation_host_set_widgets(void *opaque, const uint16_t * p0)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.set_widgets) return;
    navigation_out(binding_); binding_->host.set_widgets(binding_->host.opaque, p0); navigation_back(binding_);
}
static void navigation_host_draw_sprite(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.draw_sprite) return;
    navigation_out(binding_); binding_->host.draw_sprite(binding_->host.opaque, p0, p1, p2); navigation_back(binding_);
}
static void navigation_host_draw_string(void *opaque, const uint8_t * p0, uint8_t p1, uint16_t p2, uint16_t p3)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.draw_string) return;
    navigation_out(binding_); binding_->host.draw_string(binding_->host.opaque, p0, p1, p2, p3); navigation_back(binding_);
}
static void navigation_host_draw_name(void *opaque, const fe2_object * p0, uint8_t p1, uint16_t p2, uint16_t p3)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.draw_name) return;
    navigation_out(binding_); binding_->host.draw_name(binding_->host.opaque, p0, p1, p2, p3); navigation_back(binding_);
}
static void navigation_host_format_table_text(void *opaque, uint16_t p0, const fe2_text_table * p1, const fe2_call_args * p2, fe2_text_out * p3)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.format_table_text) return;
    navigation_out(binding_); binding_->host.format_table_text(binding_->host.opaque, p0, p1, p2, p3); navigation_back(binding_);
}
static void navigation_host_show_message(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.show_message) return;
    navigation_out(binding_); binding_->host.show_message(binding_->host.opaque, p0, p1); navigation_back(binding_);
}
static void navigation_host_play_effect(void *opaque, uint16_t p0)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.play_effect) return;
    navigation_out(binding_); binding_->host.play_effect(binding_->host.opaque, p0); navigation_back(binding_);
}
static uint8_t navigation_host_handle_flags(void *opaque, const fe2_object_list * p0, uint8_t p1)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    uint8_t result_;
    if (!binding_->host.handle_flags) {
        navigation_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    navigation_out(binding_); result_ = binding_->host.handle_flags(binding_->host.opaque, p0, p1); navigation_back(binding_);
    return result_;
}
static void navigation_host_copy_objects(void *opaque, fe2_object_list * p0, const fe2_object_list * p1)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.copy_objects) return;
    navigation_out(binding_); binding_->host.copy_objects(binding_->host.opaque, p0, p1); navigation_back(binding_);
}
static void navigation_host_clear_objects(void *opaque, const fe2_call_args * p0)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.clear_objects) return;
    navigation_out(binding_); binding_->host.clear_objects(binding_->host.opaque, p0); navigation_back(binding_);
}
static void navigation_host_remove_object(void *opaque, fe2_object * p0)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.remove_object) return;
    navigation_out(binding_); binding_->host.remove_object(binding_->host.opaque, p0); navigation_back(binding_);
}
static void navigation_host_visit_objects(void *opaque, fe2_object_list * p0, fe2_navigation_visitor p1, void * p2)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.visit_objects) return;
    navigation_out(binding_); binding_->host.visit_objects(binding_->host.opaque, p0, p1, p2); navigation_back(binding_);
}
static void navigation_host_visit_projected(void *opaque, fe2_object_list * p0, fe2_navigation_screen_visitor p1, void * p2)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.visit_projected) return;
    navigation_out(binding_); binding_->host.visit_projected(binding_->host.opaque, p0, p1, p2); navigation_back(binding_);
}
static void navigation_host_refresh_objects(void *opaque, fe2_object_list * p0)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.refresh_objects) return;
    navigation_out(binding_); binding_->host.refresh_objects(binding_->host.opaque, p0); navigation_back(binding_);
}
static void navigation_host_set_objects_time(void *opaque, fe2_object_list * p0, uint64_t p1)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.set_objects_time) return;
    navigation_out(binding_); binding_->host.set_objects_time(binding_->host.opaque, p0, p1); navigation_back(binding_);
}
static fe2_object * navigation_host_lookup_object(void *opaque, uint8_t p0, const fe2_call_args * p1)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    fe2_object * result_;
    if (!binding_->host.lookup_object) {
        navigation_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    navigation_out(binding_); result_ = binding_->host.lookup_object(binding_->host.opaque, p0, p1); navigation_back(binding_);
    return result_;
}
static fe2_navigation_model navigation_host_model(void *opaque, uint16_t p0)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    fe2_navigation_model result_;
    if (!binding_->host.model) {
        navigation_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    navigation_out(binding_); result_ = binding_->host.model(binding_->host.opaque, p0); navigation_back(binding_);
    return result_;
}
static void navigation_host_render_map(void *opaque, const fe2_call_args * p0)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.render_map) return;
    navigation_out(binding_); binding_->host.render_map(binding_->host.opaque, p0); navigation_back(binding_);
}
static void navigation_host_draw_object(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.draw_object) return;
    navigation_out(binding_); binding_->host.draw_object(binding_->host.opaque, p0, p1); navigation_back(binding_);
}
static uint16_t navigation_host_view_angles(void *opaque, uint16_t * p0)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.view_angles) {
        navigation_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    navigation_out(binding_); result_ = binding_->host.view_angles(binding_->host.opaque, p0); navigation_back(binding_);
    return result_;
}
static fe2_system_seeds navigation_host_current_system(void *opaque)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    fe2_system_seeds result_;
    if (!binding_->host.current_system) {
        navigation_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    navigation_out(binding_); result_ = binding_->host.current_system(binding_->host.opaque); navigation_back(binding_);
    return result_;
}
static void navigation_host_system_action(void *opaque, const fe2_system_seeds * p0, const fe2_call_args * p1)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.system_action) return;
    navigation_out(binding_); binding_->host.system_action(binding_->host.opaque, p0, p1); navigation_back(binding_);
}
static void navigation_host_motion_update(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.motion_update) return;
    navigation_out(binding_); binding_->host.motion_update(binding_->host.opaque, p0, p1); navigation_back(binding_);
}
static void navigation_host_reference_update(void *opaque, fe2_object * p0)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.reference_update) return;
    navigation_out(binding_); binding_->host.reference_update(binding_->host.opaque, p0); navigation_back(binding_);
}
static fe2_scalar navigation_host_normalize_word(void *opaque, fe2_scalar p0)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    fe2_scalar result_;
    if (!binding_->host.normalize_word) {
        navigation_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    navigation_out(binding_); result_ = binding_->host.normalize_word(binding_->host.opaque, p0); navigation_back(binding_);
    return result_;
}
static fe2_scalar navigation_host_multiply(void *opaque, fe2_scalar p0, fe2_scalar p1)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    fe2_scalar result_;
    if (!binding_->host.multiply) {
        navigation_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    navigation_out(binding_); result_ = binding_->host.multiply(binding_->host.opaque, p0, p1); navigation_back(binding_);
    return result_;
}
static void navigation_host_basis_b(void *opaque, int16_t p0[9])
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.basis_b) return;
    navigation_out(binding_); binding_->host.basis_b(binding_->host.opaque, p0); navigation_back(binding_);
}
static void navigation_host_transform_vector(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_navigation_overlay *binding_ = opaque;
    if (!binding_->host.transform_vector) return;
    navigation_out(binding_); binding_->host.transform_vector(binding_->host.opaque, p0, p1); navigation_back(binding_);
}
static void navigation_forward_all(fe2_navigation_services *v)
{
    v->set_panel_mode = navigation_host_set_panel_mode;
    v->poll_key = navigation_host_poll_key;
    v->forward_key = navigation_host_forward_key;
    v->show_panel = navigation_host_show_panel;
    v->restore_view = navigation_host_restore_view;
    v->set_controls = navigation_host_set_controls;
    v->set_pointer = navigation_host_set_pointer;
    v->set_widgets = navigation_host_set_widgets;
    v->draw_sprite = navigation_host_draw_sprite;
    v->draw_string = navigation_host_draw_string;
    v->draw_name = navigation_host_draw_name;
    v->format_table_text = navigation_host_format_table_text;
    v->show_message = navigation_host_show_message;
    v->play_effect = navigation_host_play_effect;
    v->handle_flags = navigation_host_handle_flags;
    v->copy_objects = navigation_host_copy_objects;
    v->clear_objects = navigation_host_clear_objects;
    v->remove_object = navigation_host_remove_object;
    v->visit_objects = navigation_host_visit_objects;
    v->visit_projected = navigation_host_visit_projected;
    v->refresh_objects = navigation_host_refresh_objects;
    v->set_objects_time = navigation_host_set_objects_time;
    v->lookup_object = navigation_host_lookup_object;
    v->model = navigation_host_model;
    v->render_map = navigation_host_render_map;
    v->draw_object = navigation_host_draw_object;
    v->view_angles = navigation_host_view_angles;
    v->current_system = navigation_host_current_system;
    v->system_action = navigation_host_system_action;
    v->motion_update = navigation_host_motion_update;
    v->reference_update = navigation_host_reference_update;
    v->normalize_word = navigation_host_normalize_word;
    v->multiply = navigation_host_multiply;
    v->basis_b = navigation_host_basis_b;
    v->transform_vector = navigation_host_transform_vector;
}
