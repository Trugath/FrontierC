/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
static fe2_scalar systems_host_normalize_dword(void *opaque, uint32_t p0)
{
    fe2_session_systems_overlay *binding_ = opaque;
    fe2_scalar result_;
    if (!binding_->host.normalize_dword) {
        systems_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    systems_out(binding_); result_ = binding_->host.normalize_dword(binding_->host.opaque, p0); systems_back(binding_);
    return result_;
}
static fe2_scalar systems_host_multiply(void *opaque, fe2_scalar p0, fe2_scalar p1)
{
    fe2_session_systems_overlay *binding_ = opaque;
    fe2_scalar result_;
    if (!binding_->host.multiply) {
        systems_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    systems_out(binding_); result_ = binding_->host.multiply(binding_->host.opaque, p0, p1); systems_back(binding_);
    return result_;
}
static fe2_scalar systems_host_divide(void *opaque, fe2_scalar p0, fe2_scalar p1)
{
    fe2_session_systems_overlay *binding_ = opaque;
    fe2_scalar result_;
    if (!binding_->host.divide) {
        systems_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    systems_out(binding_); result_ = binding_->host.divide(binding_->host.opaque, p0, p1); systems_back(binding_);
    return result_;
}
static fe2_scalar systems_host_square_root(void *opaque, fe2_scalar p0)
{
    fe2_session_systems_overlay *binding_ = opaque;
    fe2_scalar result_;
    if (!binding_->host.square_root) {
        systems_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    systems_out(binding_); result_ = binding_->host.square_root(binding_->host.opaque, p0); systems_back(binding_);
    return result_;
}
static fe2_scalar systems_host_normalize_word(void *opaque, fe2_scalar p0)
{
    fe2_session_systems_overlay *binding_ = opaque;
    fe2_scalar result_;
    if (!binding_->host.normalize_word) {
        systems_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    systems_out(binding_); result_ = binding_->host.normalize_word(binding_->host.opaque, p0); systems_back(binding_);
    return result_;
}
static fe2_scalar systems_host_add(void *opaque, fe2_scalar p0, fe2_scalar p1)
{
    fe2_session_systems_overlay *binding_ = opaque;
    fe2_scalar result_;
    if (!binding_->host.add) {
        systems_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    systems_out(binding_); result_ = binding_->host.add(binding_->host.opaque, p0, p1); systems_back(binding_);
    return result_;
}
static uint32_t systems_host_to_signed_dword(void *opaque, fe2_scalar p0)
{
    fe2_session_systems_overlay *binding_ = opaque;
    uint32_t result_;
    if (!binding_->host.to_signed_dword) {
        systems_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    systems_out(binding_); result_ = binding_->host.to_signed_dword(binding_->host.opaque, p0); systems_back(binding_);
    return result_;
}
static uint32_t systems_host_to_unsigned_dword(void *opaque, fe2_scalar p0)
{
    fe2_session_systems_overlay *binding_ = opaque;
    uint32_t result_;
    if (!binding_->host.to_unsigned_dword) {
        systems_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    systems_out(binding_); result_ = binding_->host.to_unsigned_dword(binding_->host.opaque, p0); systems_back(binding_);
    return result_;
}
static void systems_host_basis_a(void *opaque, fe2_object * p0)
{
    fe2_session_systems_overlay *binding_ = opaque;
    if (!binding_->host.basis_a) return;
    systems_out(binding_); binding_->host.basis_a(binding_->host.opaque, p0); systems_back(binding_);
}
static void systems_host_basis_b(void *opaque, fe2_object * p0)
{
    fe2_session_systems_overlay *binding_ = opaque;
    if (!binding_->host.basis_b) return;
    systems_out(binding_); binding_->host.basis_b(binding_->host.opaque, p0); systems_back(binding_);
}
static void systems_host_wide_product(void *opaque, const fe2_call_args * p0, uint16_t p1[4])
{
    fe2_session_systems_overlay *binding_ = opaque;
    if (!binding_->host.wide_product) return;
    systems_out(binding_); binding_->host.wide_product(binding_->host.opaque, p0, p1); systems_back(binding_);
}
static void systems_host_interpolate(void *opaque, fe2_object * p0, const fe2_call_args * p1)
{
    fe2_session_systems_overlay *binding_ = opaque;
    if (!binding_->host.interpolate) return;
    systems_out(binding_); binding_->host.interpolate(binding_->host.opaque, p0, p1); systems_back(binding_);
}
static void systems_host_rescale_vector(void *opaque, uint32_t p0[3], const fe2_call_args * p1)
{
    fe2_session_systems_overlay *binding_ = opaque;
    if (!binding_->host.rescale_vector) return;
    systems_out(binding_); binding_->host.rescale_vector(binding_->host.opaque, p0, p1); systems_back(binding_);
}
static fe2_object * systems_host_allocate_object(void *opaque, const fe2_call_args * p0)
{
    fe2_session_systems_overlay *binding_ = opaque;
    fe2_object * result_;
    if (!binding_->host.allocate_object) {
        systems_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    systems_out(binding_); result_ = binding_->host.allocate_object(binding_->host.opaque, p0); systems_back(binding_);
    return result_;
}
static uint32_t systems_host_object_reference(void *opaque, const fe2_object * p0)
{
    fe2_session_systems_overlay *binding_ = opaque;
    uint32_t result_;
    if (!binding_->host.object_reference) {
        systems_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    systems_out(binding_); result_ = binding_->host.object_reference(binding_->host.opaque, p0); systems_back(binding_);
    return result_;
}
static fe2_object * systems_host_object_from_reference(void *opaque, uint32_t p0)
{
    fe2_session_systems_overlay *binding_ = opaque;
    fe2_object * result_;
    if (!binding_->host.object_from_reference) {
        systems_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    systems_out(binding_); result_ = binding_->host.object_from_reference(binding_->host.opaque, p0); systems_back(binding_);
    return result_;
}
static void systems_host_set_object_flags(void *opaque, fe2_object * p0, uint8_t p1)
{
    fe2_session_systems_overlay *binding_ = opaque;
    if (!binding_->host.set_object_flags) return;
    systems_out(binding_); binding_->host.set_object_flags(binding_->host.opaque, p0, p1); systems_back(binding_);
}
static const fe2_model_header * systems_host_model(void *opaque, uint16_t p0)
{
    fe2_session_systems_overlay *binding_ = opaque;
    const fe2_model_header * result_;
    if (!binding_->host.model) {
        systems_missing(binding_);
        memset(&result_, 0, sizeof(result_)); return result_;
    }
    systems_out(binding_); result_ = binding_->host.model(binding_->host.opaque, p0); systems_back(binding_);
    return result_;
}
static void systems_host_format_table_text(void *opaque, uint16_t p0)
{
    fe2_session_systems_overlay *binding_ = opaque;
    if (!binding_->host.format_table_text) return;
    systems_out(binding_); binding_->host.format_table_text(binding_->host.opaque, p0); systems_back(binding_);
}
static void systems_host_generate_name(void *opaque, uint16_t p0, uint32_t p1, char p2[FE2_NAME_TEXT_BYTES])
{
    fe2_session_systems_overlay *binding_ = opaque;
    if (!binding_->host.generate_name) return;
    systems_out(binding_); binding_->host.generate_name(binding_->host.opaque, p0, p1, p2); systems_back(binding_);
}
static void systems_forward_all(fe2_system_services *v)
{
    v->normalize_dword = systems_host_normalize_dword;
    v->multiply = systems_host_multiply;
    v->divide = systems_host_divide;
    v->square_root = systems_host_square_root;
    v->normalize_word = systems_host_normalize_word;
    v->add = systems_host_add;
    v->to_signed_dword = systems_host_to_signed_dword;
    v->to_unsigned_dword = systems_host_to_unsigned_dword;
    v->basis_a = systems_host_basis_a;
    v->basis_b = systems_host_basis_b;
    v->wide_product = systems_host_wide_product;
    v->interpolate = systems_host_interpolate;
    v->rescale_vector = systems_host_rescale_vector;
    v->allocate_object = systems_host_allocate_object;
    v->object_reference = systems_host_object_reference;
    v->object_from_reference = systems_host_object_from_reference;
    v->set_object_flags = systems_host_set_object_flags;
    v->model = systems_host_model;
    v->format_table_text = systems_host_format_table_text;
    v->generate_name = systems_host_generate_name;
}
