/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
static uint32_t offences_host_random(void *opaque)
{
    fe2_session_offences_overlay *binding_ = opaque;
    uint32_t result_;
    if (!binding_->host.random) {
        offences_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    offences_out(binding_); result_ = binding_->host.random(binding_->host.opaque); offences_back(binding_);
    return result_;
}
static uint16_t offences_host_random_below(void *opaque, uint16_t p0)
{
    fe2_session_offences_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.random_below) {
        offences_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    offences_out(binding_); result_ = binding_->host.random_below(binding_->host.opaque, p0); offences_back(binding_);
    return result_;
}
static void offences_host_visit_objects(void *opaque, fe2_object_list * p0, fe2_offence_visitor p1, void * p2)
{
    fe2_session_offences_overlay *binding_ = opaque;
    if (!binding_->host.visit_objects) return;
    offences_out(binding_); binding_->host.visit_objects(binding_->host.opaque, p0, p1, p2); offences_back(binding_);
}
static void offences_host_show_message(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_offences_overlay *binding_ = opaque;
    if (!binding_->host.show_message) return;
    offences_out(binding_); binding_->host.show_message(binding_->host.opaque, p0, p1); offences_back(binding_);
}
static void offences_host_play_effect(void *opaque, uint16_t p0)
{
    fe2_session_offences_overlay *binding_ = opaque;
    if (!binding_->host.play_effect) return;
    offences_out(binding_); binding_->host.play_effect(binding_->host.opaque, p0); offences_back(binding_);
}
static void offences_host_check_music(void *opaque)
{
    fe2_session_offences_overlay *binding_ = opaque;
    if (!binding_->host.check_music) return;
    offences_out(binding_); binding_->host.check_music(binding_->host.opaque); offences_back(binding_);
}
static void offences_host_select_music(void *opaque, uint16_t p0)
{
    fe2_session_offences_overlay *binding_ = opaque;
    if (!binding_->host.select_music) return;
    offences_out(binding_); binding_->host.select_music(binding_->host.opaque, p0); offences_back(binding_);
}
static void offences_host_prepare_child(void *opaque, const fe2_call_args * p0)
{
    fe2_session_offences_overlay *binding_ = opaque;
    if (!binding_->host.prepare_child) return;
    offences_out(binding_); binding_->host.prepare_child(binding_->host.opaque, p0); offences_back(binding_);
}
static fe2_object * offences_host_create_object(void *opaque, const fe2_call_args * p0)
{
    fe2_session_offences_overlay *binding_ = opaque;
    fe2_object * result_;
    if (!binding_->host.create_object) {
        offences_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    offences_out(binding_); result_ = binding_->host.create_object(binding_->host.opaque, p0); offences_back(binding_);
    return result_;
}
static void offences_host_format_table_text(void *opaque, uint16_t p0, const fe2_text_table * p1, fe2_text_out * p2)
{
    fe2_session_offences_overlay *binding_ = opaque;
    if (!binding_->host.format_table_text) return;
    offences_out(binding_); binding_->host.format_table_text(binding_->host.opaque, p0, p1, p2); offences_back(binding_);
}
static void offences_forward_all(fe2_offence_services *v)
{
    v->random = offences_host_random;
    v->random_below = offences_host_random_below;
    v->visit_objects = offences_host_visit_objects;
    v->show_message = offences_host_show_message;
    v->play_effect = offences_host_play_effect;
    v->check_music = offences_host_check_music;
    v->select_music = offences_host_select_music;
    v->prepare_child = offences_host_prepare_child;
    v->create_object = offences_host_create_object;
    v->format_table_text = offences_host_format_table_text;
}
