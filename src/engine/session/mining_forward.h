/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
static fe2_object * mining_host_create_object(void *opaque, const fe2_call_args * p0)
{
    fe2_session_mining_overlay *binding_ = opaque;
    fe2_object * result_;
    if (!binding_->host.create_object) {
        mining_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    mining_out(binding_); result_ = binding_->host.create_object(binding_->host.opaque, p0); mining_back(binding_);
    return result_;
}
static void mining_host_add_position(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_mining_overlay *binding_ = opaque;
    if (!binding_->host.add_position) return;
    mining_out(binding_); binding_->host.add_position(binding_->host.opaque, p0, p1); mining_back(binding_);
}
static void mining_host_attach_object(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_mining_overlay *binding_ = opaque;
    if (!binding_->host.attach_object) return;
    mining_out(binding_); binding_->host.attach_object(binding_->host.opaque, p0, p1); mining_back(binding_);
}
static fe2_object * mining_host_lookup_object(void *opaque, uint8_t p0, const fe2_call_args * p1)
{
    fe2_session_mining_overlay *binding_ = opaque;
    fe2_object * result_;
    if (!binding_->host.lookup_object) {
        mining_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    mining_out(binding_); result_ = binding_->host.lookup_object(binding_->host.opaque, p0, p1); mining_back(binding_);
    return result_;
}
static void mining_host_visit_objects(void *opaque, fe2_object_list * p0, fe2_mining_visitor p1, fe2_mining_pass * p2)
{
    fe2_session_mining_overlay *binding_ = opaque;
    if (!binding_->host.visit_objects) return;
    mining_out(binding_); binding_->host.visit_objects(binding_->host.opaque, p0, p1, p2); mining_back(binding_);
}
static uint16_t mining_host_random(void *opaque)
{
    fe2_session_mining_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.random) {
        mining_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    mining_out(binding_); result_ = binding_->host.random(binding_->host.opaque); mining_back(binding_);
    return result_;
}
static uint16_t mining_host_random_below(void *opaque, uint16_t p0)
{
    fe2_session_mining_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.random_below) {
        mining_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    mining_out(binding_); result_ = binding_->host.random_below(binding_->host.opaque, p0); mining_back(binding_);
    return result_;
}
static uint16_t mining_host_system_profile(void *opaque, const fe2_call_args * p0)
{
    fe2_session_mining_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.system_profile) {
        mining_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    mining_out(binding_); result_ = binding_->host.system_profile(binding_->host.opaque, p0); mining_back(binding_);
    return result_;
}
static void mining_host_show_message(void *opaque, uint16_t p0, uint16_t p1, const fe2_call_args * p2)
{
    fe2_session_mining_overlay *binding_ = opaque;
    if (!binding_->host.show_message) return;
    mining_out(binding_); binding_->host.show_message(binding_->host.opaque, p0, p1, p2); mining_back(binding_);
}
static void mining_host_commit_offence(void *opaque, uint16_t p0)
{
    fe2_session_mining_overlay *binding_ = opaque;
    if (!binding_->host.commit_offence) return;
    mining_out(binding_); binding_->host.commit_offence(binding_->host.opaque, p0); mining_back(binding_);
}
static void mining_host_post_event(void *opaque, uint16_t p0)
{
    fe2_session_mining_overlay *binding_ = opaque;
    if (!binding_->host.post_event) return;
    mining_out(binding_); binding_->host.post_event(binding_->host.opaque, p0); mining_back(binding_);
}
static void mining_host_release_object(void *opaque, fe2_object * p0)
{
    fe2_session_mining_overlay *binding_ = opaque;
    if (!binding_->host.release_object) return;
    mining_out(binding_); binding_->host.release_object(binding_->host.opaque, p0); mining_back(binding_);
}
static void mining_host_format_table_text(void *opaque, uint16_t p0, const fe2_text_table * p1, fe2_text_out * p2)
{
    fe2_session_mining_overlay *binding_ = opaque;
    if (!binding_->host.format_table_text) return;
    mining_out(binding_); binding_->host.format_table_text(binding_->host.opaque, p0, p1, p2); mining_back(binding_);
}
static void mining_forward_all(fe2_mining_services *v)
{
    v->create_object = mining_host_create_object;
    v->add_position = mining_host_add_position;
    v->attach_object = mining_host_attach_object;
    v->lookup_object = mining_host_lookup_object;
    v->visit_objects = mining_host_visit_objects;
    v->random = mining_host_random;
    v->random_below = mining_host_random_below;
    v->system_profile = mining_host_system_profile;
    v->show_message = mining_host_show_message;
    v->commit_offence = mining_host_commit_offence;
    v->post_event = mining_host_post_event;
    v->release_object = mining_host_release_object;
    v->format_table_text = mining_host_format_table_text;
}
