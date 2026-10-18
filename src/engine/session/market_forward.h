/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
static uint32_t market_host_random(void *opaque, uint32_t * p0)
{
    fe2_session_market_overlay *binding_ = opaque;
    uint32_t result_;
    if (!binding_->host.random) {
        market_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    market_out(binding_); result_ = binding_->host.random(binding_->host.opaque, p0); market_back(binding_);
    return result_;
}
static uint16_t market_host_random_below(void *opaque, uint16_t p0)
{
    fe2_session_market_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.random_below) {
        market_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    market_out(binding_); result_ = binding_->host.random_below(binding_->host.opaque, p0); market_back(binding_);
    return result_;
}
static void market_host_visit_objects(void *opaque, fe2_object_list * p0, fe2_market_visitor p1, void * p2)
{
    fe2_session_market_overlay *binding_ = opaque;
    if (!binding_->host.visit_objects) return;
    market_out(binding_); binding_->host.visit_objects(binding_->host.opaque, p0, p1, p2); market_back(binding_);
}
static fe2_object * market_host_lookup_object(void *opaque, uint8_t p0, const fe2_call_args * p1)
{
    fe2_session_market_overlay *binding_ = opaque;
    fe2_object * result_;
    if (!binding_->host.lookup_object) {
        market_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    market_out(binding_); result_ = binding_->host.lookup_object(binding_->host.opaque, p0, p1); market_back(binding_);
    return result_;
}
static void market_host_system_profile(void *opaque, fe2_market_draft * p0)
{
    fe2_session_market_overlay *binding_ = opaque;
    if (!binding_->host.system_profile) return;
    market_out(binding_); binding_->host.system_profile(binding_->host.opaque, p0); market_back(binding_);
}
static fe2_system_trade market_host_system_trade(void *opaque, fe2_market_draft * p0)
{
    fe2_session_market_overlay *binding_ = opaque;
    fe2_system_trade result_;
    if (!binding_->host.system_trade) {
        market_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    market_out(binding_); result_ = binding_->host.system_trade(binding_->host.opaque, p0); market_back(binding_);
    return result_;
}
static void market_host_system_summary(void *opaque, fe2_market_draft * p0)
{
    fe2_session_market_overlay *binding_ = opaque;
    if (!binding_->host.system_summary) return;
    market_out(binding_); binding_->host.system_summary(binding_->host.opaque, p0); market_back(binding_);
}
static uint16_t market_host_find_candidate(void *opaque, fe2_market_draft * p0)
{
    fe2_session_market_overlay *binding_ = opaque;
    uint16_t result_;
    if (!binding_->host.find_candidate) {
        market_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    market_out(binding_); result_ = binding_->host.find_candidate(binding_->host.opaque, p0); market_back(binding_);
    return result_;
}
static void market_host_show_message(void *opaque, uint16_t p0, const fe2_call_args * p1)
{
    fe2_session_market_overlay *binding_ = opaque;
    if (!binding_->host.show_message) return;
    market_out(binding_); binding_->host.show_message(binding_->host.opaque, p0, p1); market_back(binding_);
}
static void market_host_select_music(void *opaque, uint16_t p0)
{
    fe2_session_market_overlay *binding_ = opaque;
    if (!binding_->host.select_music) return;
    market_out(binding_); binding_->host.select_music(binding_->host.opaque, p0); market_back(binding_);
}
static void market_host_format_text(void *opaque, uint16_t p0, const fe2_text_table * p1, fe2_text_out * p2)
{
    fe2_session_market_overlay *binding_ = opaque;
    if (!binding_->host.format_text) return;
    market_out(binding_); binding_->host.format_text(binding_->host.opaque, p0, p1, p2); market_back(binding_);
}
static void market_host_format_number(void *opaque, uint16_t p0, const fe2_call_args * p1, fe2_text_out * p2)
{
    fe2_session_market_overlay *binding_ = opaque;
    if (!binding_->host.format_number) return;
    market_out(binding_); binding_->host.format_number(binding_->host.opaque, p0, p1, p2); market_back(binding_);
}
static void market_forward_all(fe2_market_services *v)
{
    v->random = market_host_random;
    v->random_below = market_host_random_below;
    v->visit_objects = market_host_visit_objects;
    v->lookup_object = market_host_lookup_object;
    v->system_profile = market_host_system_profile;
    v->system_trade = market_host_system_trade;
    v->system_summary = market_host_system_summary;
    v->find_candidate = market_host_find_candidate;
    v->show_message = market_host_show_message;
    v->select_music = market_host_select_music;
    v->format_text = market_host_format_text;
    v->format_number = market_host_format_number;
}
