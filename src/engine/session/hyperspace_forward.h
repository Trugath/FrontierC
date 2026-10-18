/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
static fe2_ship_stats hyperspace_host_ship_stats(void *opaque, uint16_t p0)
{
    fe2_session_hyperspace_overlay *binding_ = opaque;
    fe2_ship_stats result_;
    if (!binding_->host.ship_stats) {
        hyperspace_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    hyperspace_out(binding_); result_ = binding_->host.ship_stats(binding_->host.opaque, p0); hyperspace_back(binding_);
    return result_;
}
static fe2_jump_target hyperspace_host_target_system(void *opaque)
{
    fe2_session_hyperspace_overlay *binding_ = opaque;
    fe2_jump_target result_;
    if (!binding_->host.target_system) {
        hyperspace_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    hyperspace_out(binding_); result_ = binding_->host.target_system(binding_->host.opaque); hyperspace_back(binding_);
    return result_;
}
static void hyperspace_host_system_action(void *opaque, const fe2_jump_target * p0, const fe2_call_args * p1)
{
    fe2_session_hyperspace_overlay *binding_ = opaque;
    if (!binding_->host.system_action) return;
    hyperspace_out(binding_); binding_->host.system_action(binding_->host.opaque, p0, p1); hyperspace_back(binding_);
}
static void hyperspace_host_show_message(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_hyperspace_overlay *binding_ = opaque;
    if (!binding_->host.show_message) return;
    hyperspace_out(binding_); binding_->host.show_message(binding_->host.opaque, p0, p1); hyperspace_back(binding_);
}
static void hyperspace_host_post_event(void *opaque, uint16_t p0)
{
    fe2_session_hyperspace_overlay *binding_ = opaque;
    if (!binding_->host.post_event) return;
    hyperspace_out(binding_); binding_->host.post_event(binding_->host.opaque, p0); hyperspace_back(binding_);
}
static void hyperspace_host_clear_objects(void *opaque, const fe2_call_args * p0)
{
    fe2_session_hyperspace_overlay *binding_ = opaque;
    if (!binding_->host.clear_objects) return;
    hyperspace_out(binding_); binding_->host.clear_objects(binding_->host.opaque, p0); hyperspace_back(binding_);
}
static void hyperspace_host_set_object_flags(void *opaque, fe2_object * p0, uint8_t p1)
{
    fe2_session_hyperspace_overlay *binding_ = opaque;
    if (!binding_->host.set_object_flags) return;
    hyperspace_out(binding_); binding_->host.set_object_flags(binding_->host.opaque, p0, p1); hyperspace_back(binding_);
}
static uint16_t hyperspace_host_random_below(void *opaque, uint16_t p0)
{
    fe2_session_hyperspace_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.random_below) {
        hyperspace_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    hyperspace_out(binding_); result_ = binding_->host.random_below(binding_->host.opaque, p0); hyperspace_back(binding_);
    return result_;
}
static uint32_t hyperspace_host_random(void *opaque, uint32_t * p0)
{
    fe2_session_hyperspace_overlay *binding_ = opaque;
    uint32_t result_;
    if (!binding_->host.random) {
        hyperspace_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    hyperspace_out(binding_); result_ = binding_->host.random(binding_->host.opaque, p0); hyperspace_back(binding_);
    return result_;
}
static void hyperspace_host_interpolate(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_hyperspace_overlay *binding_ = opaque;
    if (!binding_->host.interpolate) return;
    hyperspace_out(binding_); binding_->host.interpolate(binding_->host.opaque, p0, p1); hyperspace_back(binding_);
}
static void hyperspace_host_play_effect(void *opaque, uint16_t p0)
{
    fe2_session_hyperspace_overlay *binding_ = opaque;
    if (!binding_->host.play_effect) return;
    hyperspace_out(binding_); binding_->host.play_effect(binding_->host.opaque, p0); hyperspace_back(binding_);
}
static void hyperspace_host_misjump(void *opaque, fe2_object * p0, uint8_t p1)
{
    fe2_session_hyperspace_overlay *binding_ = opaque;
    if (!binding_->host.misjump) return;
    hyperspace_out(binding_); binding_->host.misjump(binding_->host.opaque, p0, p1); hyperspace_back(binding_);
}
static void hyperspace_forward_all(fe2_hyperspace_services *v)
{
    v->ship_stats = hyperspace_host_ship_stats;
    v->target_system = hyperspace_host_target_system;
    v->system_action = hyperspace_host_system_action;
    v->show_message = hyperspace_host_show_message;
    v->post_event = hyperspace_host_post_event;
    v->clear_objects = hyperspace_host_clear_objects;
    v->set_object_flags = hyperspace_host_set_object_flags;
    v->random_below = hyperspace_host_random_below;
    v->random = hyperspace_host_random;
    v->interpolate = hyperspace_host_interpolate;
    v->play_effect = hyperspace_host_play_effect;
    v->misjump = hyperspace_host_misjump;
}
