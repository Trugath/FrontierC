/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
static void commander_host_set_panel_mode(void *opaque, uint16_t p0)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.set_panel_mode) return;
    commander_out(binding_); binding_->host.set_panel_mode(binding_->host.opaque, p0); commander_back(binding_);
}
static int commander_host_poll_key(void *opaque, uint8_t * p0)
{
    fe2_session_commander_overlay *binding_ = opaque;
    int result_;
    if (!binding_->host.poll_key) {
        commander_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    commander_out(binding_); result_ = binding_->host.poll_key(binding_->host.opaque, p0); commander_back(binding_);
    return result_;
}
static void commander_host_forward_key(void *opaque, uint8_t p0)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.forward_key) return;
    commander_out(binding_); binding_->host.forward_key(binding_->host.opaque, p0); commander_back(binding_);
}
static void commander_host_show_panel(void *opaque)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.show_panel) return;
    commander_out(binding_); binding_->host.show_panel(binding_->host.opaque); commander_back(binding_);
}
static void commander_host_restore_view(void *opaque)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.restore_view) return;
    commander_out(binding_); binding_->host.restore_view(binding_->host.opaque); commander_back(binding_);
}
static void commander_host_clear_panel(void *opaque)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.clear_panel) return;
    commander_out(binding_); binding_->host.clear_panel(binding_->host.opaque); commander_back(binding_);
}
static void commander_host_present(void *opaque)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.present) return;
    commander_out(binding_); binding_->host.present(binding_->host.opaque); commander_back(binding_);
}
static void commander_host_set_controls(void *opaque, uint16_t p0, uint16_t p1, uint8_t p2)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.set_controls) return;
    commander_out(binding_); binding_->host.set_controls(binding_->host.opaque, p0, p1, p2); commander_back(binding_);
}
static void commander_host_set_widgets(void *opaque, const uint16_t * p0)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.set_widgets) return;
    commander_out(binding_); binding_->host.set_widgets(binding_->host.opaque, p0); commander_back(binding_);
}
static void commander_host_add_hotspot(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2, uint16_t p3, uint16_t p4)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.add_hotspot) return;
    commander_out(binding_); binding_->host.add_hotspot(binding_->host.opaque, p0, p1, p2, p3, p4); commander_back(binding_);
}
static void commander_host_draw_sprite(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.draw_sprite) return;
    commander_out(binding_); binding_->host.draw_sprite(binding_->host.opaque, p0, p1, p2); commander_back(binding_);
}
static void commander_host_draw_icon(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.draw_icon) return;
    commander_out(binding_); binding_->host.draw_icon(binding_->host.opaque, p0, p1, p2); commander_back(binding_);
}
static void commander_host_draw_text(void *opaque, uint16_t p0, uint8_t p1, uint16_t p2, uint16_t p3, const fe2_call_args * p4)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.draw_text) return;
    commander_out(binding_); binding_->host.draw_text(binding_->host.opaque, p0, p1, p2, p3, p4); commander_back(binding_);
}
static void commander_host_draw_string(void *opaque, const char * p0, uint8_t p1, uint16_t p2, uint16_t p3)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.draw_string) return;
    commander_out(binding_); binding_->host.draw_string(binding_->host.opaque, p0, p1, p2, p3); commander_back(binding_);
}
static void commander_host_draw_edit_box(void *opaque, uint16_t p0, uint16_t p1, uint16_t p2)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.draw_edit_box) return;
    commander_out(binding_); binding_->host.draw_edit_box(binding_->host.opaque, p0, p1, p2); commander_back(binding_);
}
static void commander_host_edit_text(void *opaque, char * p0, uint16_t p1, uint8_t p2, uint16_t p3, uint16_t p4, const fe2_call_args * p5)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.edit_text) return;
    commander_out(binding_); binding_->host.edit_text(binding_->host.opaque, p0, p1, p2, p3, p4, p5); commander_back(binding_);
}
static void commander_host_format_table_text(void *opaque, uint16_t p0, const fe2_text_table * p1, fe2_text_out * p2)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.format_table_text) return;
    commander_out(binding_); binding_->host.format_table_text(binding_->host.opaque, p0, p1, p2); commander_back(binding_);
}
static void commander_host_clear_space_dust(void *opaque)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.clear_space_dust) return;
    commander_out(binding_); binding_->host.clear_space_dust(binding_->host.opaque); commander_back(binding_);
}
static void commander_host_wait_tick(void *opaque)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.wait_tick) return;
    commander_out(binding_); binding_->host.wait_tick(binding_->host.opaque); commander_back(binding_);
}
static void commander_host_post_event(void *opaque, uint16_t p0)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.post_event) return;
    commander_out(binding_); binding_->host.post_event(binding_->host.opaque, p0); commander_back(binding_);
}
static uint8_t * commander_host_object_record(void *opaque, uint8_t p0)
{
    fe2_session_commander_overlay *binding_ = opaque;
    uint8_t * result_;
    if (!binding_->host.object_record) {
        commander_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    commander_out(binding_); result_ = binding_->host.object_record(binding_->host.opaque, p0); commander_back(binding_);
    return result_;
}
static fe2_save_image commander_host_save_image(void *opaque)
{
    fe2_session_commander_overlay *binding_ = opaque;
    fe2_save_image result_;
    if (!binding_->host.save_image) {
        commander_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    commander_out(binding_); result_ = binding_->host.save_image(binding_->host.opaque); commander_back(binding_);
    return result_;
}
static void commander_host_restart_game(void *opaque)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.restart_game) return;
    commander_out(binding_); binding_->host.restart_game(binding_->host.opaque); commander_back(binding_);
}
static void commander_host_quit_game(void *opaque)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.quit_game) return;
    commander_out(binding_); binding_->host.quit_game(binding_->host.opaque); commander_back(binding_);
}
static void commander_host_check_music(void *opaque)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.check_music) return;
    commander_out(binding_); binding_->host.check_music(binding_->host.opaque); commander_back(binding_);
}
static void commander_host_select_music(void *opaque, uint16_t p0)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.select_music) return;
    commander_out(binding_); binding_->host.select_music(binding_->host.opaque, p0); commander_back(binding_);
}
static void commander_host_play_effect(void *opaque, uint16_t p0)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.play_effect) return;
    commander_out(binding_); binding_->host.play_effect(binding_->host.opaque, p0); commander_back(binding_);
}
static void commander_host_stop_effects(void *opaque)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.stop_effects) return;
    commander_out(binding_); binding_->host.stop_effects(binding_->host.opaque); commander_back(binding_);
}
static void commander_host_reset_music(void *opaque)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.reset_music) return;
    commander_out(binding_); binding_->host.reset_music(binding_->host.opaque); commander_back(binding_);
}
static void commander_host_select_panel(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.select_panel) return;
    commander_out(binding_); binding_->host.select_panel(binding_->host.opaque, p0, p1); commander_back(binding_);
}
static void commander_host_refresh_cockpit(void *opaque)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.refresh_cockpit) return;
    commander_out(binding_); binding_->host.refresh_cockpit(binding_->host.opaque); commander_back(binding_);
}
static void commander_host_show_message(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_commander_overlay *binding_ = opaque;
    if (!binding_->host.show_message) return;
    commander_out(binding_); binding_->host.show_message(binding_->host.opaque, p0, p1); commander_back(binding_);
}
static void commander_forward_all(fe2_commander_services *v)
{
    v->set_panel_mode = commander_host_set_panel_mode;
    v->poll_key = commander_host_poll_key;
    v->forward_key = commander_host_forward_key;
    v->show_panel = commander_host_show_panel;
    v->restore_view = commander_host_restore_view;
    v->clear_panel = commander_host_clear_panel;
    v->present = commander_host_present;
    v->set_controls = commander_host_set_controls;
    v->set_widgets = commander_host_set_widgets;
    v->add_hotspot = commander_host_add_hotspot;
    v->draw_sprite = commander_host_draw_sprite;
    v->draw_icon = commander_host_draw_icon;
    v->draw_text = commander_host_draw_text;
    v->draw_string = commander_host_draw_string;
    v->draw_edit_box = commander_host_draw_edit_box;
    v->edit_text = commander_host_edit_text;
    v->format_table_text = commander_host_format_table_text;
    v->clear_space_dust = commander_host_clear_space_dust;
    v->wait_tick = commander_host_wait_tick;
    v->post_event = commander_host_post_event;
    v->object_record = commander_host_object_record;
    v->save_image = commander_host_save_image;
    v->restart_game = commander_host_restart_game;
    v->quit_game = commander_host_quit_game;
    v->check_music = commander_host_check_music;
    v->select_music = commander_host_select_music;
    v->play_effect = commander_host_play_effect;
    v->stop_effects = commander_host_stop_effects;
    v->reset_music = commander_host_reset_music;
    v->select_panel = commander_host_select_panel;
    v->refresh_cockpit = commander_host_refresh_cockpit;
    v->show_message = commander_host_show_message;
}
