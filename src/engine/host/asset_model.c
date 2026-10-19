/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "asset_model.h"

#include "asset_files.h"
#include "asset_ini.h"
#include "asset_program.h"
#include "asset_util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_i64(const char *text, long long *out)
{
    char *end;
    long long value;
    if (!text || !text[0]) return -1;
    value = strtoll(text, &end, 0);
    if (end == text || *end) return -1;
    *out = value;
    return 0;
}

static int directory_of(const char *path, char *out, size_t cap)
{
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *cut = !slash ? back : !back ? slash : (slash > back ? slash : back);
    size_t n = cut ? (size_t)(cut - path) : 0;
    if (!n || n >= cap) return fe2_asset_fail("Could not read %s", path);
    memcpy(out, path, n);
    out[n] = 0;
    return 0;
}

static int boolean_value(const char *text, int *out)
{
    if (!text) { *out = 0; return 0; }
    if (!strcmp(text, "1") || !strcmp(text, "true") || !strcmp(text, "yes") || !strcmp(text, "on")) { *out = 1; return 0; }
    if (!strcmp(text, "0") || !strcmp(text, "false") || !strcmp(text, "no") || !strcmp(text, "off")) { *out = 0; return 0; }
    return -1;
}

static int take_bytes(const char *text, uint8_t **out, size_t *count, int grouped)
{
    const char *cursor = text ? text : "";
    uint8_t *bytes = NULL;
    size_t n = 0, cap = 0, row = 0;
    while (*cursor) {
        while (*cursor && isspace((unsigned char)*cursor)) {
            if (*cursor == '\n') {
                if (grouped && row) return fe2_asset_fail("Expected a four-byte encoded vector record");
                row = 0;
            }
            ++cursor;
        }
        if (!*cursor) break;
        {
            char *end;
            long long value = strtoll(cursor, &end, 0);
            if (end == cursor || value < 0 || value > 255) return fe2_asset_fail("Model element exceeds its declared width");
            if (n == cap) {
                size_t next = cap ? cap * 2 : 64;
                uint8_t *grown = (uint8_t *)realloc(bytes, next);
                if (!grown) { free(bytes); return -1; }
                bytes = grown;
                cap = next;
            }
            bytes[n++] = (uint8_t)value;
            ++row;
            if (grouped && row == 4) row = 0;
            cursor = end;
            if (*cursor == ',') ++cursor;
        }
    }
    if (grouped && row) { free(bytes); return fe2_asset_fail("Expected a four-byte encoded vector record"); }
    *out = bytes;
    *count = n;
    return 0;
}

static int model_name_ok(const char *name, const char *prefix)
{
    size_t n = strlen(prefix);
    return strlen(name) == n + 4 && strncmp(name, prefix, n) == 0 && name[n] == '_' &&
           isdigit((unsigned char)name[n + 1]) && isdigit((unsigned char)name[n + 2]) &&
           isdigit((unsigned char)name[n + 3]);
}

static int insert_at(fe2_buf *buf, size_t at, const uint8_t *extra, size_t count)
{
    size_t i;
    if (at > buf->count) return -1;
    for (i = 0; i < count; ++i) if (fe2_buf_byte(buf, 0)) return -1;
    if (count) {
        memmove(buf->data + at + count, buf->data + at, buf->count - count - at);
        memcpy(buf->data + at, extra, count);
    }
    return 0;
}

static int compile_model(const fe2_ini_section *section, const char *base, const char *names_path,
                         const char *prefix, uint8_t **record, size_t *record_n, size_t *program_before_contact)
{
    const char *file = fe2_ini_get(section, "file");
    char path[4096];
    fe2_ini model;
    fe2_ini_section *header, *extension, *vertices, *classification, *program, *contact, *offsets;
    uint8_t *vertex_bytes = NULL, *class_bytes = NULL, *prefix_bytes = NULL;
    size_t vertex_n = 0, class_n = 0, prefix_n = 0;
    fe2_buf rendered, contact_bytes, offset_bytes, packed;
    unsigned entries[256];
    size_t entry_n = 0;
    int status = -1;
    (void)program_before_contact;
    memset(&model, 0, sizeof model);
    memset(&rendered, 0, sizeof rendered);
    memset(&contact_bytes, 0, sizeof contact_bytes);
    memset(&offset_bytes, 0, sizeof offset_bytes);
    memset(&packed, 0, sizeof packed);
    if (!file || fe2_path_join(path, sizeof path, base, file) || fe2_ini_load(&model, path))
        return fe2_asset_fail("Could not read model %s", section->name);
    header = fe2_ini_section_find(&model, "header");
    extension = fe2_ini_section_find(&model, "extension");
    vertices = fe2_ini_section_find(&model, "vertices");
    classification = fe2_ini_section_find(&model, "classification");
    program = fe2_ini_section_find(&model, "program");
    contact = fe2_ini_section_find(&model, "contact");
    offsets = fe2_ini_section_find(&model, "contact_offsets");
    if (!header || !extension || !vertices || !classification || !program ||
        header->count != 3 || !fe2_ini_get(vertices, "records") || vertices->count != 1 ||
        !fe2_ini_get(classification, "records") || classification->count != 1) {
        fe2_asset_fail("Invalid model sections");
        goto done;
    }
    {
        const char *keys[] = {"scale_shift", "reserved_0c", "culling_radius"};
        unsigned words[3];
        size_t i;
        for (i = 0; i < 3; ++i) {
            long long value;
            const char *text = fe2_ini_get(header, keys[i]);
            if (!text || parse_i64(text, &value) || value < 0 || value > 65535) {
                fe2_asset_fail("Model header word out of range");
                goto done;
            }
            words[i] = (unsigned)value;
        }
        if (take_bytes(fe2_ini_get(vertices, "records"), &vertex_bytes, &vertex_n, 1) ||
            take_bytes(fe2_ini_get(classification, "records"), &class_bytes, &class_n, 1) ||
            take_bytes(fe2_ini_get(extension, "bytes"), &prefix_bytes, &prefix_n, 0)) goto done;
        if (fe2_ini_get(extension, "instance_programs")) {
            const char *rows = fe2_ini_get(extension, "instance_programs");
            const char *cursor = rows;
            if (prefix_n != 16) { fe2_asset_fail("Instance directory prefix must end at model header +20h"); goto done; }
            while (*cursor) {
                char *end_line;
                char line[128];
                long long selector, offset;
                char *split;
                while (*cursor && isspace((unsigned char)*cursor)) ++cursor;
                if (!*cursor) break;
                end_line = (char *)cursor;
                while (*end_line && *end_line != '\n') ++end_line;
                if ((size_t)(end_line - cursor) >= sizeof line) { fe2_asset_fail("Instance entry requires a byte selector and program offset"); goto done; }
                memcpy(line, cursor, (size_t)(end_line - cursor));
                line[end_line - cursor] = 0;
                cursor = *end_line ? end_line + 1 : end_line;
                if (line[0] && line[strlen(line) - 1] == '\r') line[strlen(line) - 1] = 0;
                split = line;
                while (*split && *split != ' ' && *split != '\t' && *split != ',') ++split;
                if (!*split) { fe2_asset_fail("Instance entry requires a byte selector and program offset"); goto done; }
                {
                    char *next = split;
                    while (*next == ' ' || *next == '\t' || *next == ',') ++next;
                    *split = 0;
                    split = next;
                }
                while (*split && isspace((unsigned char)split[strlen(split) - 1])) split[strlen(split) - 1] = 0;
                if (parse_i64(line, &selector) || parse_i64(split, &offset) || selector != 0x20 + (long long)entry_n ||
                    selector > 255 || offset < 0 || offset > 65535 || entry_n == 256) {
                    fe2_asset_fail("Instance directory must have contiguous byte selectors from 20h");
                    goto done;
                }
                entries[entry_n++] = (unsigned)offset;
            }
            if (!entry_n) { fe2_asset_fail("Instance directory cannot be empty"); goto done; }
        } else if (extension->count != 1) {
            fe2_asset_fail("Invalid model extension fields");
            goto done;
        }
        if (fe2_ini_get(program, "instructions") && program->count == 1) {
            if (fe2_compile_program(fe2_ini_get(program, "instructions"), base, names_path, &rendered)) goto done;
        } else if (fe2_ini_get(program, "bytes") && program->count == 1) {
            uint8_t *raw = NULL;
            size_t raw_n = 0;
            if (take_bytes(fe2_ini_get(program, "bytes"), &raw, &raw_n, 0) || fe2_buf_add(&rendered, raw, raw_n)) {
                free(raw); goto done;
            }
            free(raw);
        } else { fe2_asset_fail("Invalid model renderer source fields"); goto done; }
        {
            size_t i;
            for (i = 0; i < entry_n; ++i) if (entries[i] >= rendered.count) {
                fe2_asset_fail("Instance directory entry leaves renderer program");
                goto done;
            }
        }
        if (contact) {
            char contact_path[4096];
            char *body;
            long long at;
            const char *contact_file = fe2_ini_get(contact, "file");
            const char *contact_at = fe2_ini_get(contact, "program_offset");
            if (contact->count != 2 || !contact_file || !contact_at || parse_i64(contact_at, &at) ||
                at < 0 || (size_t)at > rendered.count) {
                fe2_asset_fail("Contact insertion leaves renderer program");
                goto done;
            }
            if (offsets) {
                const char *offset_file = fe2_ini_get(offsets, "file");
                const char *offset_at = fe2_ini_get(offsets, "program_offset");
                long long same;
                if (offsets->count != 2 || !offset_file || !offset_at || parse_i64(offset_at, &same) || same != at ||
                    at < 2 || rendered.data[at - 2] || rendered.data[at - 1]) {
                    fe2_asset_fail("Contact offset walk requires the shared preceding zero word");
                    goto done;
                }
                if (fe2_path_join(contact_path, sizeof contact_path, base, offset_file)) goto done;
                body = fe2_read_text(contact_path, NULL);
                if (!body || fe2_compile_offsets(body, &offset_bytes) ||
                    insert_at(&rendered, (size_t)at, offset_bytes.data, offset_bytes.count)) {
                    free(body); goto done;
                }
                free(body);
                at += (long long)offset_bytes.count;
            }
            if (fe2_path_join(contact_path, sizeof contact_path, base, contact_file)) goto done;
            body = fe2_read_text(contact_path, NULL);
            if (!body || fe2_compile_contact(body, &contact_bytes) ||
                insert_at(&rendered, (size_t)at, contact_bytes.data, contact_bytes.count)) {
                free(body); goto done;
            }
            free(body);
            if (prefix_n < 12 || prefix_bytes[10] + 256u * prefix_bytes[11] !=
                16u + prefix_n + (fe2_ini_get(extension, "instance_programs") ? entry_n * 2 : 0) + vertex_n + class_n + (size_t)at) {
                /* Recomputed after the extension addresses are appended. Checked below. */
            }
            {
                size_t extension_n = prefix_n + (fe2_ini_get(extension, "instance_programs") ? entry_n * 2 : 0);
                size_t program_offset = 16 + extension_n + vertex_n + class_n;
                if (prefix_n < 12 || prefix_bytes[10] + 256u * prefix_bytes[11] != program_offset + (size_t)at) {
                    fe2_asset_fail("Contact source does not match model header pointer");
                    goto done;
                }
            }
        }
        {
            size_t extension_n = prefix_n;
            uint8_t *extension_bytes;
            size_t vertex_offset, class_offset, program_offset, i;
            int absent = 0;
            unsigned header_words[3];
            const char *keys[] = {"scale_shift", "reserved_0c", "culling_radius"};
            if (fe2_ini_get(extension, "instance_programs")) {
                size_t base_addr = 16 + prefix_n + 2 * entry_n + vertex_n + class_n;
                extension_n = prefix_n + 2 * entry_n;
                extension_bytes = (uint8_t *)malloc(extension_n ? extension_n : 1);
                if (!extension_bytes) goto done;
                memcpy(extension_bytes, prefix_bytes, prefix_n);
                for (i = 0; i < entry_n; ++i) {
                    size_t address = base_addr + entries[i];
                    if (address > 65535) { free(extension_bytes); fe2_asset_fail("Instance entry leaves model-relative word addressing"); goto done; }
                    extension_bytes[prefix_n + 2 * i] = (uint8_t)address;
                    extension_bytes[prefix_n + 2 * i + 1] = (uint8_t)(address >> 8);
                }
            } else {
                extension_bytes = prefix_bytes;
                prefix_bytes = NULL;
            }
            if ((strcmp(prefix, "main_model") == 0 && extension_n != 0 && extension_n != 14) ||
                (strcmp(prefix, "auxiliary_model") == 0 && extension_n != 0 && extension_n != 118 && extension_n != 198)) {
                if (extension_bytes != prefix_bytes) free(extension_bytes);
                fe2_asset_fail("Unexpected model header extension size");
                goto done;
            }
            if (vertex_n * 16 > 65535 || class_n + 4 > 65535) {
                if (extension_bytes != prefix_bytes) free(extension_bytes);
                fe2_asset_fail("Model working buffer size exceeds a word");
                goto done;
            }
            vertex_offset = 16 + extension_n;
            class_offset = vertex_offset + vertex_n;
            program_offset = class_offset + class_n;
            for (i = 0; i < 3; ++i) {
                long long value;
                parse_i64(fe2_ini_get(header, keys[i]), &value);
                header_words[i] = (unsigned)value;
            }
            if (boolean_value(fe2_ini_get(section, "absent_classification"), &absent) || (absent && class_n)) {
                if (extension_bytes != prefix_bytes) free(extension_bytes);
                fe2_asset_fail("Absent classification has records");
                goto done;
            }
            if (fe2_buf_u16(&packed, (unsigned)program_offset) || fe2_buf_u16(&packed, (unsigned)vertex_offset) ||
                fe2_buf_u16(&packed, (unsigned)(vertex_n * 16)) ||
                fe2_buf_u16(&packed, absent ? 0 : (unsigned)class_offset) ||
                fe2_buf_u16(&packed, absent ? 0 : (unsigned)(class_n + 4)) ||
                fe2_buf_u16(&packed, header_words[0]) || fe2_buf_u16(&packed, header_words[1]) ||
                fe2_buf_u16(&packed, header_words[2]) ||
                fe2_buf_add(&packed, extension_bytes, extension_n) ||
                fe2_buf_add(&packed, vertex_bytes, vertex_n) ||
                fe2_buf_add(&packed, class_bytes, class_n) ||
                fe2_buf_add(&packed, rendered.data, rendered.count)) {
                if (extension_bytes != prefix_bytes) free(extension_bytes);
                goto done;
            }
            if (extension_bytes != prefix_bytes) free(extension_bytes);
        }
    }
    *record = packed.data;
    *record_n = packed.count;
    packed.data = NULL;
    status = 0;
done:
    fe2_buf_free(&packed);
    fe2_buf_free(&rendered);
    fe2_buf_free(&contact_bytes);
    fe2_buf_free(&offset_bytes);
    free(vertex_bytes);
    free(class_bytes);
    free(prefix_bytes);
    fe2_ini_free(&model);
    return status;
}

int fe2_compile_bank(const char *ini_path, const char *names_path, fe2_bank_image *bank)
{
    fe2_ini ini;
    fe2_ini_section *bank_section, *trailer;
    char base[4096], path[4096];
    const char *prefix, *directory_name;
    char **directory = NULL;
    size_t directory_n = 0, s, named = 0;
    int explicit_layout, status = -1;
    long long start = 0, directory_offset = 2, end = 0, position;
    int have_end = 0;
    struct built { char name[64]; long long start, end; uint8_t *data; size_t count; } *built = NULL;
    size_t built_n = 0;
    memset(bank, 0, sizeof *bank);
    memset(&ini, 0, sizeof ini);
    if (directory_of(ini_path, base, sizeof base) || fe2_ini_load(&ini, ini_path))
        return fe2_asset_fail("Could not read %s", ini_path);
    bank_section = fe2_ini_section_find(&ini, "bank");
    if (!bank_section) { fe2_asset_fail("Invalid main model bank layout"); goto done; }
    prefix = fe2_ini_get(bank_section, "prefix");
    if (!prefix) prefix = fe2_ini_get(bank_section, "module") &&
        !strcmp(fe2_ini_get(bank_section, "module"), "intro") ? "intro_model" : "main_model";
    if (strcmp(prefix, "main_model") && strcmp(prefix, "auxiliary_model") && strcmp(prefix, "intro_model")) {
        fe2_asset_fail("Invalid model bank prefix"); goto done;
    }
    if (!fe2_ini_get(bank_section, "module") ||
        (strcmp(fe2_ini_get(bank_section, "module"), "frontier") &&
         strcmp(fe2_ini_get(bank_section, "module"), "intro"))) {
        fe2_asset_fail("Invalid main model bank layout"); goto done;
    }
    {
        long long count = 240;
        if (fe2_ini_get(bank_section, "directory_count") &&
            (parse_i64(fe2_ini_get(bank_section, "directory_count"), &count) || count != 240)) {
            fe2_asset_fail("Invalid main model bank layout"); goto done;
        }
    }
    explicit_layout = fe2_ini_get(bank_section, "start") != NULL;
    if (explicit_layout) {
        if (!fe2_ini_get(bank_section, "directory_offset") || !fe2_ini_get(bank_section, "end") ||
            parse_i64(fe2_ini_get(bank_section, "start"), &start) ||
            parse_i64(fe2_ini_get(bank_section, "directory_offset"), &directory_offset) ||
            parse_i64(fe2_ini_get(bank_section, "end"), &end) ||
            directory_offset != start + 2 || start >= end || end > 0x100000) {
            fe2_asset_fail("Invalid main model bank layout"); goto done;
        }
        have_end = 1;
    }
    directory_name = fe2_ini_get(bank_section, "directory");
    if (!directory_name || fe2_path_join(path, sizeof path, base, directory_name)) goto done;
    {
        char *text = fe2_read_text(path, NULL), *cursor;
        if (!text) { fe2_asset_fail("Could not read %s", path); goto done; }
        cursor = text;
        while (*cursor) {
            char *eol = strchr(cursor, '\n');
            char *tab;
            char **grown;
            if (eol) *eol = 0;
            if (cursor[0] && cursor[strlen(cursor) - 1] == '\r') cursor[strlen(cursor) - 1] = 0;
            tab = strchr(cursor, '\t');
            if (!tab) { free(text); fe2_asset_fail("Model slots must be consecutive and ordered"); goto done; }
            *tab = 0;
            {
                long long slot;
                if (parse_i64(cursor, &slot) || slot != (long long)directory_n) {
                    free(text); fe2_asset_fail("Model slots must be consecutive and ordered"); goto done;
                }
            }
            grown = (char **)realloc(directory, (directory_n + 1) * sizeof *grown);
            if (!grown) { free(text); goto done; }
            directory = grown;
            directory[directory_n] = strdup(tab + 1);
            if (!directory[directory_n]) { free(text); goto done; }
            if (strcmp(directory[directory_n], "-") != 0) ++named;
            ++directory_n;
            if (!eol) break;
            cursor = eol + 1;
            if (!*cursor) break;
        }
        free(text);
        if (directory_n != 240) { fe2_asset_fail("Model directory count changed"); goto done; }
    }
    position = directory_offset + 2 * 240;
    for (s = 0; s < ini.count; ++s) {
        fe2_ini_section *section = &ini.sections[s];
        uint8_t *record = NULL;
        size_t record_n = 0;
        struct built *grown;
        long long model_start, model_end;
        if (!strcmp(section->name, "bank") || !strcmp(section->name, "trailer")) continue;
        if (!model_name_ok(section->name, prefix)) { fe2_asset_fail("Invalid model name"); goto done; }
        if (compile_model(section, base, names_path, prefix, &record, &record_n, NULL)) goto done;
        model_start = position;
        model_end = position + (long long)record_n;
        if (fe2_ini_get(section, "start")) {
            long long want_start, want_end;
            if (!fe2_ini_get(section, "end") || parse_i64(fe2_ini_get(section, "start"), &want_start) ||
                parse_i64(fe2_ini_get(section, "end"), &want_end) || want_start != model_start || want_end != model_end ||
                (have_end && model_end > end)) {
                free(record); fe2_asset_fail("Model layout changed: %s", section->name); goto done;
            }
        } else if (have_end && model_end > end) {
            free(record); fe2_asset_fail("Model layout changed: %s", section->name); goto done;
        }
        grown = realloc(built, (built_n + 1) * sizeof *grown);
        if (!grown) { free(record); goto done; }
        built = grown;
        memset(&built[built_n], 0, sizeof built[built_n]);
        snprintf(built[built_n].name, sizeof built[built_n].name, "%s", section->name);
        built[built_n].start = model_start;
        built[built_n].end = model_end;
        built[built_n].data = record;
        built[built_n].count = record_n;
        ++built_n;
        position = model_end;
    }
    {
        long long population = explicit_layout && !strcmp(prefix, "main_model") ? 230 : (long long)named;
        size_t empty = 0, i;
        if (fe2_ini_get(bank_section, "model_count")) {
            if (parse_i64(fe2_ini_get(bank_section, "model_count"), &population)) {
                fe2_asset_fail("Main model directory population changed"); goto done;
            }
        }
        for (i = 0; i < directory_n; ++i) if (!strcmp(directory[i], "-")) ++empty;
        if ((long long)built_n != population || (long long)empty != 240 - population) {
            fe2_asset_fail("Main model directory population changed"); goto done;
        }
        if (!have_end) { end = position; have_end = 1; }
        if (end < start || end - start > 65535 || position != end) {
            fe2_asset_fail("Main model bank size exceeds a word"); goto done;
        }
        for (i = 0; i < built_n; ++i) {
            size_t slot, hits = 0;
            for (slot = 0; slot < directory_n; ++slot)
                if (!strcmp(directory[slot], built[i].name)) ++hits;
            if (hits != 1) { fe2_asset_fail("Model bank has missing or unreferenced models"); goto done; }
        }
        for (i = 0; i < directory_n; ++i) {
            size_t model;
            if (!strcmp(directory[i], "-")) continue;
            for (model = 0; model < built_n; ++model) if (!strcmp(built[model].name, directory[i])) break;
            if (model == built_n) { fe2_asset_fail("Model bank has missing or unreferenced models"); goto done; }
        }
    }
    {
        uint8_t *trailer_bytes = NULL;
        size_t trailer_n = 0;
        long long file_end = end;
        trailer = fe2_ini_section_find(&ini, "trailer");
        if (trailer) {
            if (fe2_ini_get(trailer, "bytes")) {
                if (take_bytes(fe2_ini_get(trailer, "bytes"), &trailer_bytes, &trailer_n, 0)) goto done;
            } else if (fe2_ini_get(trailer, "text")) {
                const char *text = fe2_ini_get(trailer, "text");
                trailer_n = strlen(text);
                trailer_bytes = (uint8_t *)malloc(trailer_n ? trailer_n : 1);
                if (!trailer_bytes) goto done;
                memcpy(trailer_bytes, text, trailer_n);
            }
            if (fe2_ini_get(trailer, "end")) {
                if (parse_i64(fe2_ini_get(trailer, "end"), &file_end)) { free(trailer_bytes); goto done; }
            } else file_end = end + (long long)trailer_n;
        }
        if (file_end < end) { free(trailer_bytes); fe2_asset_fail("Main model bank size exceeds a word"); goto done; }
        {
            size_t image_n = (size_t)(file_end - start);
            size_t tail = (size_t)(file_end - end);
            uint8_t *image;
            size_t i;
            if (trailer_n > tail) image_n = (size_t)(end - start) + trailer_n;
            image = (uint8_t *)calloc(image_n ? image_n : 1, 1);
            if (!image) { free(trailer_bytes); goto done; }
            image[0] = (uint8_t)(end - start);
            image[1] = (uint8_t)((end - start) >> 8);
            for (i = 0; i < directory_n; ++i) {
                unsigned relative = 0;
                size_t at = (size_t)(directory_offset - start) + 2 * i;
                if (strcmp(directory[i], "-") != 0) {
                    size_t model;
                    for (model = 0; model < built_n; ++model) if (!strcmp(built[model].name, directory[i])) break;
                    relative = (unsigned)(built[model].start - directory_offset);
                }
                image[at] = (uint8_t)relative;
                image[at + 1] = (uint8_t)(relative >> 8);
                bank->offsets[i] = strcmp(directory[i], "-") ? (unsigned)(built[i < built_n ? 0 : 0].start) : 0;
            }
            for (i = 0; i < directory_n; ++i) {
                size_t model;
                if (!strcmp(directory[i], "-")) { bank->offsets[i] = 0; continue; }
                for (model = 0; model < built_n; ++model) if (!strcmp(built[model].name, directory[i])) break;
                bank->offsets[i] = (unsigned)(built[model].start - start);
            }
            for (i = 0; i < built_n; ++i) {
                size_t at = (size_t)(built[i].start - start);
                if (at == 0 || built[i].start - start > 0xffff) {
                    free(image); free(trailer_bytes);
                    fe2_asset_fail("%s has no address in the model bank", built[i].name);
                    goto done;
                }
                memcpy(image + at, built[i].data, built[i].count);
            }
            if (trailer_n) memcpy(image + (size_t)(end - start), trailer_bytes, trailer_n);
            free(trailer_bytes);
            bank->image = image;
            bank->image_count = (uint32_t)image_n;
        }
        bank->models = calloc(built_n ? built_n : 1, sizeof *bank->models);
        if (!bank->models) goto done;
        for (s = 0; s < directory_n; ++s) {
            size_t model;
            if (!strcmp(directory[s], "-")) continue;
            for (model = 0; model < built_n; ++model) if (!strcmp(built[model].name, directory[s])) break;
            snprintf(bank->models[bank->count].name, sizeof bank->models[bank->count].name, "%s", directory[s]);
            bank->models[bank->count].slot = (unsigned)s;
            bank->models[bank->count].offset = bank->offsets[s];
            bank->models[bank->count].data = built[model].data;
            bank->models[bank->count].count = (uint32_t)built[model].count;
            built[model].data = NULL;
            ++bank->count;
        }
    }
    status = 0;
done:
    if (status) fe2_bank_free(bank);
    for (s = 0; s < built_n; ++s) free(built[s].data);
    free(built);
    for (s = 0; s < directory_n; ++s) free(directory[s]);
    free(directory);
    fe2_ini_free(&ini);
    return status;
}

void fe2_bank_free(fe2_bank_image *bank)
{
    size_t i;
    if (!bank) return;
    free(bank->image);
    for (i = 0; i < bank->count; ++i) free(bank->models[i].data);
    free(bank->models);
    memset(bank, 0, sizeof *bank);
}
