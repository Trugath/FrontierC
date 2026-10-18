/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
static int map_host_density_samples(void *opaque, uint16_t p0, uint16_t p1, fe2_map_samples * p2)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.density_samples) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.density_samples(binding_->host.opaque, p0, p1, p2); map_back(binding_);
    return result_;
}
static int map_host_random_below(void *opaque, uint16_t p0, uint16_t * p1)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.random_below) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.random_below(binding_->host.opaque, p0, p1); map_back(binding_);
    return result_;
}
static int map_host_random(void *opaque, uint32_t * p0)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.random) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.random(binding_->host.opaque, p0); map_back(binding_);
    return result_;
}
static int map_host_vector_length(void *opaque, const int16_t p0[3], uint16_t * p1)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.vector_length) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.vector_length(binding_->host.opaque, p0, p1); map_back(binding_);
    return result_;
}
static int map_host_system_details(void *opaque, uint32_t p0, unsigned p1, fe2_map_details * p2)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.system_details) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.system_details(binding_->host.opaque, p0, p1, p2); map_back(binding_);
    return result_;
}
static int map_host_travel(void *opaque, const fe2_object * p0, uint32_t p1, uint16_t * p2, int * p3)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.travel) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.travel(binding_->host.opaque, p0, p1, p2, p3); map_back(binding_);
    return result_;
}
static int map_host_generate_system(void *opaque, const fe2_map_query * p0, fe2_record_space * p1, uint8_t * p2)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.generate_system) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.generate_system(binding_->host.opaque, p0, p1, p2); map_back(binding_);
    return result_;
}
static int map_host_expand_text(void *opaque, const char * p0, const fe2_call_args * p1, char * p2, size_t p3)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.expand_text) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.expand_text(binding_->host.opaque, p0, p1, p2, p3); map_back(binding_);
    return result_;
}
static int map_host_format_number(void *opaque, uint16_t p0, const fe2_call_args * p1, char * p2, size_t p3)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.format_number) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.format_number(binding_->host.opaque, p0, p1, p2, p3); map_back(binding_);
    return result_;
}
static int map_host_draw_text(void *opaque, const char * p0, uint8_t p1, uint16_t p2, uint16_t p3, int p4)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.draw_text) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.draw_text(binding_->host.opaque, p0, p1, p2, p3, p4); map_back(binding_);
    return result_;
}
static int map_host_draw_text_flow(void *opaque, const char * p0, uint8_t p1, uint16_t * p2, uint16_t * p3)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.draw_text_flow) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.draw_text_flow(binding_->host.opaque, p0, p1, p2, p3); map_back(binding_);
    return result_;
}
static int map_host_draw_resident_text(void *opaque, uint16_t p0, uint8_t p1, uint16_t p2, uint16_t p3, const fe2_call_args * p4)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.draw_resident_text) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.draw_resident_text(binding_->host.opaque, p0, p1, p2, p3, p4); map_back(binding_);
    return result_;
}
static int map_host_draw_button(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.draw_button) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.draw_button(binding_->host.opaque, p0, p1, p2); map_back(binding_);
    return result_;
}
static int map_host_clear_panel(void *opaque)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.clear_panel) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.clear_panel(binding_->host.opaque); map_back(binding_);
    return result_;
}
static int map_host_show_panel(void *opaque)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.show_panel) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.show_panel(binding_->host.opaque); map_back(binding_);
    return result_;
}
static int map_host_present(void *opaque)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.present) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.present(binding_->host.opaque); map_back(binding_);
    return result_;
}
static int map_host_set_palette(void *opaque, const uint8_t * p0, unsigned p1)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.set_palette) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.set_palette(binding_->host.opaque, p0, p1); map_back(binding_);
    return result_;
}
static int map_host_set_controls(void *opaque, uint16_t p0, uint8_t p1)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.set_controls) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.set_controls(binding_->host.opaque, p0, p1); map_back(binding_);
    return result_;
}
static int map_host_set_panel_mode(void *opaque, uint16_t p0)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.set_panel_mode) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.set_panel_mode(binding_->host.opaque, p0); map_back(binding_);
    return result_;
}
static int map_host_reset_view(void *opaque)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.reset_view) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.reset_view(binding_->host.opaque); map_back(binding_);
    return result_;
}
static int map_host_clear_projection(void *opaque)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.clear_projection) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.clear_projection(binding_->host.opaque); map_back(binding_);
    return result_;
}
static int map_host_primary_panel(void *opaque, unsigned p0, uint16_t p1)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.primary_panel) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.primary_panel(binding_->host.opaque, p0, p1); map_back(binding_);
    return result_;
}
static int map_host_secondary_view(void *opaque, int p0)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.secondary_view) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.secondary_view(binding_->host.opaque, p0); map_back(binding_);
    return result_;
}
static int map_host_forward_command(void *opaque, uint8_t p0)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.forward_command) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.forward_command(binding_->host.opaque, p0); map_back(binding_);
    return result_;
}
static int map_host_basis(void *opaque, uint16_t p0[2], int16_t p1[9])
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.basis) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.basis(binding_->host.opaque, p0, p1); map_back(binding_);
    return result_;
}
static int map_host_project(void *opaque, const int16_t p0[9], const int16_t p1[3], uint32_t p2[3])
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.project) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.project(binding_->host.opaque, p0, p1, p2); map_back(binding_);
    return result_;
}
static int map_host_draw_scene(void *opaque, const fe2_map_scene * p0, int p1)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.draw_scene) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.draw_scene(binding_->host.opaque, p0, p1); map_back(binding_);
    return result_;
}
static int map_host_draw_pixel(void *opaque, uint16_t p0, uint8_t p1, uint8_t p2)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.draw_pixel) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.draw_pixel(binding_->host.opaque, p0, p1, p2); map_back(binding_);
    return result_;
}
static int map_host_draw_block(void *opaque, uint16_t p0, uint16_t p1, uint8_t p2, uint8_t p3, uint8_t p4)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.draw_block) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.draw_block(binding_->host.opaque, p0, p1, p2, p3, p4); map_back(binding_);
    return result_;
}
static int map_host_input_pending(void *opaque)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.input_pending) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.input_pending(binding_->host.opaque); map_back(binding_);
    return result_;
}
static int map_host_draw_crosshair(void *opaque, uint16_t p0, uint16_t p1, uint8_t p2)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.draw_crosshair) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.draw_crosshair(binding_->host.opaque, p0, p1, p2); map_back(binding_);
    return result_;
}
static int map_host_draw_line(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2, uint16_t p3, uint8_t p4)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.draw_line) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.draw_line(binding_->host.opaque, p0, p1, p2, p3, p4); map_back(binding_);
    return result_;
}
static int map_host_hierarchy_node(void *opaque, uint16_t p0, uint16_t p1, uint16_t * p2, uint16_t * p3)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.hierarchy_node) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.hierarchy_node(binding_->host.opaque, p0, p1, p2, p3); map_back(binding_);
    return result_;
}
static int map_host_draw_body_sprite(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.draw_body_sprite) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.draw_body_sprite(binding_->host.opaque, p0, p1, p2); map_back(binding_);
    return result_;
}
static int map_host_body_scale(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2, uint16_t p3, uint16_t * p4, uint16_t * p5)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.body_scale) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.body_scale(binding_->host.opaque, p0, p1, p2, p3, p4, p5); map_back(binding_);
    return result_;
}
static int map_host_body_number(void *opaque, uint16_t p0, uint16_t p1, int p2, uint32_t * p3)
{
    fe2_session_map_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.body_number) {
        map_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    map_out(binding_); result_ = binding_->host.body_number(binding_->host.opaque, p0, p1, p2, p3); map_back(binding_);
    return result_;
}
static void map_forward_all(fe2_map_services *v)
{
    v->density_samples = map_host_density_samples;
    v->random_below = map_host_random_below;
    v->random = map_host_random;
    v->vector_length = map_host_vector_length;
    v->system_details = map_host_system_details;
    v->travel = map_host_travel;
    v->generate_system = map_host_generate_system;
    v->expand_text = map_host_expand_text;
    v->format_number = map_host_format_number;
    v->draw_text = map_host_draw_text;
    v->draw_text_flow = map_host_draw_text_flow;
    v->draw_resident_text = map_host_draw_resident_text;
    v->draw_button = map_host_draw_button;
    v->clear_panel = map_host_clear_panel;
    v->show_panel = map_host_show_panel;
    v->present = map_host_present;
    v->set_palette = map_host_set_palette;
    v->set_controls = map_host_set_controls;
    v->set_panel_mode = map_host_set_panel_mode;
    v->reset_view = map_host_reset_view;
    v->clear_projection = map_host_clear_projection;
    v->primary_panel = map_host_primary_panel;
    v->secondary_view = map_host_secondary_view;
    v->forward_command = map_host_forward_command;
    v->basis = map_host_basis;
    v->project = map_host_project;
    v->draw_scene = map_host_draw_scene;
    v->draw_pixel = map_host_draw_pixel;
    v->draw_block = map_host_draw_block;
    v->input_pending = map_host_input_pending;
    v->draw_crosshair = map_host_draw_crosshair;
    v->draw_line = map_host_draw_line;
    v->hierarchy_node = map_host_hierarchy_node;
    v->draw_body_sprite = map_host_draw_body_sprite;
    v->body_scale = map_host_body_scale;
    v->body_number = map_host_body_number;
}
