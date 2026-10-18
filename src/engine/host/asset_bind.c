/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "asset_bind.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const uint8_t *fe2_adlib_image_template;
extern size_t fe2_adlib_image_bytes;
extern const uint8_t *galaxy_density_data;
extern size_t galaxy_density_bytes;
extern const uint8_t *composite_rgb6;
extern size_t composite_rgb6_bytes;
extern const uint8_t *fe2_names_directory;
extern size_t fe2_names_directory_bytes;
extern const uint8_t *fe2_intro_model_bank;
extern const uint8_t *fe2_intro_text_directory;
extern const uint8_t *fe2_intro_orbit_scene;
extern const uint8_t *fe2_intro_scenes;
extern const uint8_t *fe2_flight_text_directory;
extern const uint8_t *fe2_mining_text_directory;
extern const uint8_t *fe2_offences_text_directory;
extern const uint8_t *fe2_interaction_text_text_directory;
extern const uint8_t *fe2_commander_text_directory;
extern const uint8_t *fe2_status_text_bytes;
extern int fe2_market_bind(const uint8_t *image, size_t count);
extern int fe2_systems_bind(const uint8_t *image, size_t count);
extern int fe2_catalogue_bind(const uint8_t *image, size_t count);
extern int fe2_input_bind(const uint8_t *image, size_t count);
extern int fe2_pointer_bind(const uint8_t *image, size_t count);
extern int fe2_pool_bind(const uint8_t *image, size_t count);
extern int fe2_motion_bind(const uint8_t *image, size_t count);
extern int fe2_contact_bind(const uint8_t *image, size_t count);
extern int fe2_position_bind(const uint8_t *image, size_t count);
extern int fe2_objects_bind(const uint8_t *image, size_t count);
extern int fe2_probe_bind(const uint8_t *image, size_t count);
extern int fe2_people_bind(const uint8_t *image, size_t count);
extern int fe2_panels_bind(const uint8_t *image, size_t count);
extern int fe2_intro_tables_bind(const uint8_t *image, size_t count);
extern int fe2_navigation_bind(const uint8_t *image, size_t count);
extern int fe2_render_bind(const uint8_t *image, size_t count);
extern int fe2_offences_bind(const uint8_t *image, size_t count);
extern int fe2_mining_bind(const uint8_t *image, size_t count);
extern int fe2_commander_bind(const uint8_t *image, size_t count);

static const fe2_asset_entry *need(const fe2_asset_archive *archive, const char *name)
{
    const fe2_asset_entry *entry = fe2_asset_archive_find(archive, name);
    if (!entry || !entry->data) return NULL;
    return entry;
}

static int bind_text(const fe2_asset_archive *archive, fe2_render_runtime_resources *resources)
{
    const fe2_asset_entry *info = need(archive, "text-info");
    size_t at;
    int have_builtin = 0, have_external = 0;
    uint64_t overlays = 0;
    if (!info) return -1;
    at = 0;
    while (at < info->count) {
        uint8_t name_len;
        uint16_t entries, kind, slot, mask;
        char key[6 + 255];
        const fe2_asset_entry *text;
        fe2_text_directory *target;
        if (info->count - at < 1 + 8) return -1;
        name_len = info->data[at++];
        if (name_len == 0 || (size_t)name_len + 8 > info->count - at) return -1;
        memcpy(key, "text/", 5);
        memcpy(key + 5, info->data + at, name_len);
        key[5 + name_len] = 0;
        at += name_len;
        entries = (uint16_t)(info->data[at] | (info->data[at + 1] << 8));
        kind = (uint16_t)(info->data[at + 2] | (info->data[at + 3] << 8));
        slot = (uint16_t)(info->data[at + 4] | (info->data[at + 5] << 8));
        mask = (uint16_t)(info->data[at + 6] | (info->data[at + 7] << 8));
        at += 8;
        text = need(archive, key);
        if (!text) return -1;
        if (kind == 0) {
            if (have_builtin) return -1;
            have_builtin = 1;
            target = &resources->text.builtin;
        } else if (kind == 1) {
            if (have_external) return -1;
            have_external = 1;
            target = &resources->text.external;
        } else if (kind == 2) {
            uint64_t bit;
            if (slot >= 64) return -1;
            bit = UINT64_C(1) << slot;
            if (overlays & bit) return -1;
            overlays |= bit;
            resources->text.overlay_mask[slot] = mask;
            target = &resources->text.overlay[slot];
        } else return -1;
        target->bytes.data = text->data;
        target->bytes.count = text->count;
        target->entries = entries;
    }
    return have_builtin && have_external ? 0 : -1;
}

static int bind_models(const fe2_asset_archive *archive, fe2_render_runtime_resources *resources)
{
    unsigned bank, slot;
    for (bank = 0; bank < 2; ++bank) {
        char key[32];
        const fe2_asset_entry *image, *offsets;
        if (snprintf(key, sizeof key, "bank/%u", bank) >= (int)sizeof key) return -1;
        image = need(archive, key);
        if (snprintf(key, sizeof key, "model-offsets/%u", bank) >= (int)sizeof key) return -1;
        offsets = need(archive, key);
        if (!image || !offsets || offsets->count != 240u * 2u) return -1;
        resources->model_banks[bank].data = image->data;
        resources->model_banks[bank].count = image->count;
        for (slot = 0; slot < 240; ++slot) {
            uint16_t offset = (uint16_t)(offsets->data[slot * 2u] | (offsets->data[slot * 2u + 1u] << 8));
            const fe2_asset_entry *model;
            resources->model_offsets[bank][slot] = offset;
            if (!offset) {
                resources->models[bank][slot].data = NULL;
                resources->models[bank][slot].count = 0;
                continue;
            }
            if (snprintf(key, sizeof key, "model/%u/%u", bank, slot) >= (int)sizeof key) return -1;
            model = need(archive, key);
            if (!model || !model->count) return -1;
            resources->models[bank][slot].data = model->data;
            resources->models[bank][slot].count = model->count;
        }
    }
    return 0;
}

static int bind_graphics(const fe2_asset_archive *archive, fe2_graphics_cache *graphics)
{
    size_t i;
    if (!graphics) return 0;
    for (i = 0; i < archive->count; ++i) {
        const fe2_asset_entry *entry = &archive->entries[i];
        unsigned long id;
        char buf[16], *end;
        fe2_render_bytes record;
        if (entry->name_len < 9 || memcmp(entry->name, "graphic/", 8) != 0) continue;
        if (entry->name_len >= sizeof buf) return -1;
        memcpy(buf, entry->name, entry->name_len);
        buf[entry->name_len] = 0;
        id = strtoul(buf + 8, &end, 10);
        if (end == buf + 8 || *end || id >= FE2_GRAPHICS_RECORDS) return -1;
        record.data = entry->data;
        record.count = entry->count;
        if (fe2_graphics_cache_bind(graphics, (unsigned)id, record)) return -1;
    }
    return 0;
}

int fe2_asset_archive_apply(const fe2_asset_archive *archive, fe2_render_runtime_resources *resources,
                            fe2_graphics_cache *graphics)
{
    const fe2_asset_entry *font, *widths;
    if (!archive || !resources) return -1;
    font = need(archive, "font");
    widths = need(archive, "widths");
    if (!font || !widths || font->count == 0 || font->count % 10 || widths->count != 256) return -1;
    if (bind_text(archive, resources) || bind_models(archive, resources) || bind_graphics(archive, graphics)) return -1;
    resources->font = font->data;
    resources->glyphs = font->count / 10u;
    resources->text.widths = widths->data;
    return 0;
}

static int bind_blob(const fe2_asset_archive *archive, const char *name, size_t size,
                     const uint8_t **pointer, size_t *bytes)
{
    const fe2_asset_entry *entry = need(archive, name);
    if (!entry || entry->count != size) return -1;
    *pointer = entry->data;
    if (bytes) *bytes = entry->count;
    return 0;
}

int fe2_asset_bind_install(const fe2_asset_archive *archive)
{
    if (!archive) return -1;
    if (bind_blob(archive, "adlib/image", 0x67dd, &fe2_adlib_image_template, &fe2_adlib_image_bytes) ||
        bind_blob(archive, "galaxy/density", 16514, &galaxy_density_data, &galaxy_density_bytes) ||
        bind_blob(archive, "composites", 1488, &composite_rgb6, &composite_rgb6_bytes) ||
        bind_blob(archive, "text/names", 3785, &fe2_names_directory, &fe2_names_directory_bytes) ||
        bind_blob(archive, "intro/models", 27440, &fe2_intro_model_bank, NULL) ||
        bind_blob(archive, "text/intro", 470, &fe2_intro_text_directory, NULL) ||
        bind_blob(archive, "intro/orbit", 286, &fe2_intro_orbit_scene, NULL) ||
        bind_blob(archive, "intro/scenes", 22u * 286u, &fe2_intro_scenes, NULL) ||
        bind_blob(archive, "text/flight", 1230, &fe2_flight_text_directory, NULL) ||
        bind_blob(archive, "text/mining", 1008, &fe2_mining_text_directory, NULL) ||
        bind_blob(archive, "text/offences", 365, &fe2_offences_text_directory, NULL) ||
        bind_blob(archive, "text/interaction", 19542, &fe2_interaction_text_text_directory, NULL) ||
        bind_blob(archive, "text/commander", 2164, &fe2_commander_text_directory, NULL) ||
        bind_blob(archive, "text/status", 3575, &fe2_status_text_bytes, NULL))
        return -1;
    {
        const fe2_asset_entry *market = need(archive, "table/market");
        const fe2_asset_entry *systems = need(archive, "table/systems");
        const fe2_asset_entry *catalogue = need(archive, "table/catalogue");
        if (!market || fe2_market_bind(market->data, market->count)) return -1;
        if (!systems || fe2_systems_bind(systems->data, systems->count)) return -1;
        if (!catalogue || fe2_catalogue_bind(catalogue->data, catalogue->count)) return -1;
    }
    {
        static const char *names[] = {
            "table/input", "table/pointer", "table/pool", "table/motion",
            "table/contact", "table/position", "table/objects", "table/probe",
            "table/people", "table/panels", "table/intro", "table/navigation", "table/render",
            "table/offences", "table/mining", "table/commander"
        };
        int (*binds[])(const uint8_t *, size_t) = {
            fe2_input_bind, fe2_pointer_bind, fe2_pool_bind, fe2_motion_bind,
            fe2_contact_bind, fe2_position_bind, fe2_objects_bind, fe2_probe_bind,
            fe2_people_bind, fe2_panels_bind, fe2_intro_tables_bind, fe2_navigation_bind,
            fe2_render_bind, fe2_offences_bind, fe2_mining_bind, fe2_commander_bind
        };
        unsigned i;
        for (i = 0; i < 16; ++i) {
            const fe2_asset_entry *entry = need(archive, names[i]);
            if (!entry || binds[i](entry->data, entry->count)) return -1;
        }
    }
    return 0;
}
