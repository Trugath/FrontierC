/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
static void status_host_clear_panel(void *opaque)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.clear_panel) return;
    status_out(binding_); binding_->host.clear_panel(binding_->host.opaque); status_back(binding_);
}
static void status_host_draw_text(void *opaque, uint16_t p0, uint8_t p1, uint16_t p2, uint16_t p3, const fe2_call_args * p4)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.draw_text) return;
    status_out(binding_); binding_->host.draw_text(binding_->host.opaque, p0, p1, p2, p3, p4); status_back(binding_);
}
static void status_host_draw_icon(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.draw_icon) return;
    status_out(binding_); binding_->host.draw_icon(binding_->host.opaque, p0, p1, p2); status_back(binding_);
}
static void status_host_draw_sprite(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.draw_sprite) return;
    status_out(binding_); binding_->host.draw_sprite(binding_->host.opaque, p0, p1, p2); status_back(binding_);
}
static void status_host_draw_frame(void *opaque, const fe2_rect * p0, uint16_t p1)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.draw_frame) return;
    status_out(binding_); binding_->host.draw_frame(binding_->host.opaque, p0, p1); status_back(binding_);
}
static void status_host_set_controls(void *opaque, uint16_t p0, uint8_t p1)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.set_controls) return;
    status_out(binding_); binding_->host.set_controls(binding_->host.opaque, p0, p1); status_back(binding_);
}
static void status_host_show_panel(void *opaque)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.show_panel) return;
    status_out(binding_); binding_->host.show_panel(binding_->host.opaque); status_back(binding_);
}
static void status_host_present(void *opaque)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.present) return;
    status_out(binding_); binding_->host.present(binding_->host.opaque); status_back(binding_);
}
static void status_host_set_widgets(void *opaque, const fe2_widget_table * p0)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.set_widgets) return;
    status_out(binding_); binding_->host.set_widgets(binding_->host.opaque, p0); status_back(binding_);
}
static void status_host_set_panel_mode(void *opaque, uint16_t p0)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.set_panel_mode) return;
    status_out(binding_); binding_->host.set_panel_mode(binding_->host.opaque, p0); status_back(binding_);
}
static void status_host_open_panel(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.open_panel) return;
    status_out(binding_); binding_->host.open_panel(binding_->host.opaque, p0, p1); status_back(binding_);
}
static void status_host_prepare_camera(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.prepare_camera) return;
    status_out(binding_); binding_->host.prepare_camera(binding_->host.opaque, p0, p1); status_back(binding_);
}
static void status_host_orient_camera(void *opaque, fe2_object * p0)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.orient_camera) return;
    status_out(binding_); binding_->host.orient_camera(binding_->host.opaque, p0); status_back(binding_);
}
static void status_host_render_scene(void *opaque, fe2_object * p0)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.render_scene) return;
    status_out(binding_); binding_->host.render_scene(binding_->host.opaque, p0); status_back(binding_);
}
static void status_host_format_text(void *opaque, uint16_t p0, const fe2_call_args * p1, char p2[FE2_TEXT_BYTES])
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.format_text) return;
    status_out(binding_); binding_->host.format_text(binding_->host.opaque, p0, p1, p2); status_back(binding_);
}
static void status_host_generate_name(void *opaque, uint16_t p0, uint32_t p1, char p2[FE2_TEXT_BYTES])
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.generate_name) return;
    status_out(binding_); binding_->host.generate_name(binding_->host.opaque, p0, p1, p2); status_back(binding_);
}
static void status_host_show_message(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.show_message) return;
    status_out(binding_); binding_->host.show_message(binding_->host.opaque, p0, p1); status_back(binding_);
}
static void status_host_format_table_text(void *opaque, uint16_t p0, const fe2_text_table * p1)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.format_table_text) return;
    status_out(binding_); binding_->host.format_table_text(binding_->host.opaque, p0, p1); status_back(binding_);
}
static uint32_t status_host_random(void *opaque)
{
    fe2_session_status_overlay *binding_ = opaque;
    uint32_t result_;
    if (!binding_->host.random) {
        status_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    status_out(binding_); result_ = binding_->host.random(binding_->host.opaque); status_back(binding_);
    return result_;
}
static uint16_t status_host_random_below(void *opaque, uint16_t p0)
{
    fe2_session_status_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.random_below) {
        status_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    status_out(binding_); result_ = binding_->host.random_below(binding_->host.opaque, p0); status_back(binding_);
    return result_;
}
static const fe2_ship_type * status_host_ship_type(void *opaque, uint16_t p0)
{
    fe2_session_status_overlay *binding_ = opaque;
    const fe2_ship_type * result_;
    if (!binding_->host.ship_type) {
        status_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    status_out(binding_); result_ = binding_->host.ship_type(binding_->host.opaque, p0); status_back(binding_);
    return result_;
}
static int status_host_poll_key(void *opaque, uint8_t * p0)
{
    fe2_session_status_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.poll_key) {
        status_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    status_out(binding_); result_ = binding_->host.poll_key(binding_->host.opaque, p0); status_back(binding_);
    return result_;
}
static void status_host_forward_key(void *opaque)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.forward_key) return;
    status_out(binding_); binding_->host.forward_key(binding_->host.opaque); status_back(binding_);
}
static void status_host_post_event(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.post_event) return;
    status_out(binding_); binding_->host.post_event(binding_->host.opaque, p0, p1); status_back(binding_);
}
static void status_host_select_music(void *opaque, uint16_t p0)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.select_music) return;
    status_out(binding_); binding_->host.select_music(binding_->host.opaque, p0); status_back(binding_);
}
static void status_host_play_effect(void *opaque, uint16_t p0)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.play_effect) return;
    status_out(binding_); binding_->host.play_effect(binding_->host.opaque, p0); status_back(binding_);
}
static void status_host_apply_impulse(void *opaque, const fe2_call_args * p0)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.apply_impulse) return;
    status_out(binding_); binding_->host.apply_impulse(binding_->host.opaque, p0); status_back(binding_);
}
static void status_host_square_vector(void *opaque, uint32_t p0[3])
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.square_vector) return;
    status_out(binding_); binding_->host.square_vector(binding_->host.opaque, p0); status_back(binding_);
}
static fe2_object * status_host_spawn_object(void *opaque, const fe2_call_args * p0)
{
    fe2_session_status_overlay *binding_ = opaque;
    fe2_object * result_;
    if (!binding_->host.spawn_object) {
        status_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    status_out(binding_); result_ = binding_->host.spawn_object(binding_->host.opaque, p0); status_back(binding_);
    return result_;
}
static fe2_object * status_host_lookup_object(void *opaque, uint8_t p0)
{
    fe2_session_status_overlay *binding_ = opaque;
    fe2_object * result_;
    if (!binding_->host.lookup_object) {
        status_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    status_out(binding_); result_ = binding_->host.lookup_object(binding_->host.opaque, p0); status_back(binding_);
    return result_;
}
static void status_host_orient_object(void *opaque, fe2_object * p0)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.orient_object) return;
    status_out(binding_); binding_->host.orient_object(binding_->host.opaque, p0); status_back(binding_);
}
static void status_host_visit_objects(void *opaque, fe2_object_list * p0, fe2_object_visitor p1, void * p2)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.visit_objects) return;
    status_out(binding_); binding_->host.visit_objects(binding_->host.opaque, p0, p1, p2); status_back(binding_);
}
static void status_host_market_service(void *opaque, uint16_t p0, uint16_t p1, fe2_call_args * p2)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.market_service) return;
    status_out(binding_); binding_->host.market_service(binding_->host.opaque, p0, p1, p2); status_back(binding_);
}
static fe2_object * status_host_create_object(void *opaque, uint16_t p0, fe2_call_args * p1)
{
    fe2_session_status_overlay *binding_ = opaque;
    fe2_object * result_;
    if (!binding_->host.create_object) {
        status_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    status_out(binding_); result_ = binding_->host.create_object(binding_->host.opaque, p0, p1); status_back(binding_);
    return result_;
}
static void status_host_drive_summary(void *opaque, uint16_t p0, const fe2_call_args * p1, uint16_t * p2, uint16_t * p3)
{
    fe2_session_status_overlay *binding_ = opaque;
    if (!binding_->host.drive_summary) return;
    status_out(binding_); binding_->host.drive_summary(binding_->host.opaque, p0, p1, p2, p3); status_back(binding_);
}
static void status_forward_all(fe2_game_services *v)
{
    v->clear_panel = status_host_clear_panel;
    v->draw_text = status_host_draw_text;
    v->draw_icon = status_host_draw_icon;
    v->draw_sprite = status_host_draw_sprite;
    v->draw_frame = status_host_draw_frame;
    v->set_controls = status_host_set_controls;
    v->show_panel = status_host_show_panel;
    v->present = status_host_present;
    v->set_widgets = status_host_set_widgets;
    v->set_panel_mode = status_host_set_panel_mode;
    v->open_panel = status_host_open_panel;
    v->prepare_camera = status_host_prepare_camera;
    v->orient_camera = status_host_orient_camera;
    v->render_scene = status_host_render_scene;
    v->format_text = status_host_format_text;
    v->generate_name = status_host_generate_name;
    v->show_message = status_host_show_message;
    v->format_table_text = status_host_format_table_text;
    v->random = status_host_random;
    v->random_below = status_host_random_below;
    v->ship_type = status_host_ship_type;
    v->poll_key = status_host_poll_key;
    v->forward_key = status_host_forward_key;
    v->post_event = status_host_post_event;
    v->select_music = status_host_select_music;
    v->play_effect = status_host_play_effect;
    v->apply_impulse = status_host_apply_impulse;
    v->square_vector = status_host_square_vector;
    v->spawn_object = status_host_spawn_object;
    v->lookup_object = status_host_lookup_object;
    v->orient_object = status_host_orient_object;
    v->visit_objects = status_host_visit_objects;
    v->market_service = status_host_market_service;
    v->create_object = status_host_create_object;
    v->drive_summary = status_host_drive_summary;
}
