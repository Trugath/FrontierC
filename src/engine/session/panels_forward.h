/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
static void panels_host_draw_sprite(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2)
{
    fe2_session_panels_overlay *binding_ = opaque;
    if (!binding_->host.draw_sprite) return;
    panels_out(binding_); binding_->host.draw_sprite(binding_->host.opaque, p0, p1, p2); panels_back(binding_);
}
static void panels_host_draw_sprite_over(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2)
{
    fe2_session_panels_overlay *binding_ = opaque;
    if (!binding_->host.draw_sprite_over) return;
    panels_out(binding_); binding_->host.draw_sprite_over(binding_->host.opaque, p0, p1, p2); panels_back(binding_);
}
static void panels_host_draw_text(void *opaque, uint16_t p0, uint8_t p1, uint16_t p2, uint16_t p3, const fe2_call_args * p4)
{
    fe2_session_panels_overlay *binding_ = opaque;
    if (!binding_->host.draw_text) return;
    panels_out(binding_); binding_->host.draw_text(binding_->host.opaque, p0, p1, p2, p3, p4); panels_back(binding_);
}
static void panels_host_draw_text_over(void *opaque, uint16_t p0, uint8_t p1, uint16_t p2, uint16_t p3, const fe2_call_args * p4)
{
    fe2_session_panels_overlay *binding_ = opaque;
    if (!binding_->host.draw_text_over) return;
    panels_out(binding_); binding_->host.draw_text_over(binding_->host.opaque, p0, p1, p2, p3, p4); panels_back(binding_);
}
static void panels_host_draw_line(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2, uint16_t p3)
{
    fe2_session_panels_overlay *binding_ = opaque;
    if (!binding_->host.draw_line) return;
    panels_out(binding_); binding_->host.draw_line(binding_->host.opaque, p0, p1, p2, p3); panels_back(binding_);
}
static void panels_host_present(void *opaque)
{
    fe2_session_panels_overlay *binding_ = opaque;
    if (!binding_->host.present) return;
    panels_out(binding_); binding_->host.present(binding_->host.opaque); panels_back(binding_);
}
static void panels_host_selection_changed(void *opaque)
{
    fe2_session_panels_overlay *binding_ = opaque;
    if (!binding_->host.selection_changed) return;
    panels_out(binding_); binding_->host.selection_changed(binding_->host.opaque); panels_back(binding_);
}
static void panels_host_install_gauges(void *opaque, fe2_gauge_callback p0)
{
    fe2_session_panels_overlay *binding_ = opaque;
    if (!binding_->host.install_gauges) return;
    panels_out(binding_); binding_->host.install_gauges(binding_->host.opaque, p0); panels_back(binding_);
}
static void panels_host_object_event(void *opaque, uint8_t p0)
{
    fe2_session_panels_overlay *binding_ = opaque;
    if (!binding_->host.object_event) return;
    panels_out(binding_); binding_->host.object_event(binding_->host.opaque, p0); panels_back(binding_);
}
static void panels_host_secondary_basis(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_panels_overlay *binding_ = opaque;
    if (!binding_->host.secondary_basis) return;
    panels_out(binding_); binding_->host.secondary_basis(binding_->host.opaque, p0, p1); panels_back(binding_);
}
static fe2_object * panels_host_linked_object(void *opaque, fe2_object * p0)
{
    fe2_session_panels_overlay *binding_ = opaque;
    fe2_object * result_;
    if (!binding_->host.linked_object) {
        panels_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    panels_out(binding_); result_ = binding_->host.linked_object(binding_->host.opaque, p0); panels_back(binding_);
    return result_;
}
static void panels_host_relative_position(void *opaque, fe2_object * p0, const fe2_object * p1, fe2_wide_vector * p2)
{
    fe2_session_panels_overlay *binding_ = opaque;
    if (!binding_->host.relative_position) return;
    panels_out(binding_); binding_->host.relative_position(binding_->host.opaque, p0, p1, p2); panels_back(binding_);
}
static fe2_model_colour panels_host_model_colour(void *opaque, uint16_t p0)
{
    fe2_session_panels_overlay *binding_ = opaque;
    fe2_model_colour result_;
    if (!binding_->host.model_colour) {
        panels_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    panels_out(binding_); result_ = binding_->host.model_colour(binding_->host.opaque, p0); panels_back(binding_);
    return result_;
}
static void panels_host_draw_marker(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2, const fe2_call_args * p3)
{
    fe2_session_panels_overlay *binding_ = opaque;
    if (!binding_->host.draw_marker) return;
    panels_out(binding_); binding_->host.draw_marker(binding_->host.opaque, p0, p1, p2, p3); panels_back(binding_);
}
static uint8_t panels_host_notify_panel(void *opaque, uint16_t p0, uint16_t p1)
{
    fe2_session_panels_overlay *binding_ = opaque;
    uint8_t result_;
    if (!binding_->host.notify_panel) {
        panels_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    panels_out(binding_); result_ = binding_->host.notify_panel(binding_->host.opaque, p0, p1); panels_back(binding_);
    return result_;
}
static void panels_host_visit_objects(void *opaque, fe2_object_list * p0, fe2_panel_visitor p1, void * p2)
{
    fe2_session_panels_overlay *binding_ = opaque;
    if (!binding_->host.visit_objects) return;
    panels_out(binding_); binding_->host.visit_objects(binding_->host.opaque, p0, p1, p2); panels_back(binding_);
}
static void panels_forward_all(fe2_panel_services *v)
{
    v->draw_sprite = panels_host_draw_sprite;
    v->draw_sprite_over = panels_host_draw_sprite_over;
    v->draw_text = panels_host_draw_text;
    v->draw_text_over = panels_host_draw_text_over;
    v->draw_line = panels_host_draw_line;
    v->present = panels_host_present;
    v->selection_changed = panels_host_selection_changed;
    v->install_gauges = panels_host_install_gauges;
    v->object_event = panels_host_object_event;
    v->secondary_basis = panels_host_secondary_basis;
    v->linked_object = panels_host_linked_object;
    v->relative_position = panels_host_relative_position;
    v->model_colour = panels_host_model_colour;
    v->draw_marker = panels_host_draw_marker;
    v->notify_panel = panels_host_notify_panel;
    v->visit_objects = panels_host_visit_objects;
}
