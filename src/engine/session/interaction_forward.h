/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
static void interaction_host_format_table_text(void *opaque, uint16_t p0, const fe2_text_table * p1, fe2_text_out * p2)
{
    fe2_session_interaction_overlay *binding_ = opaque;
    if (!binding_->host.format_table_text) return;
    interaction_out(binding_); binding_->host.format_table_text(binding_->host.opaque, p0, p1, p2); interaction_back(binding_);
}
static void interaction_forward_all(fe2_interaction_services *v)
{
    v->format_table_text = interaction_host_format_table_text;
}
