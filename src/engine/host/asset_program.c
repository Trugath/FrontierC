/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "asset_program.h"

#include "asset_files.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_i64(const char *text, long long *out)
{
    char *end;
    long long value;
    if (!text || !text[0]) return -1;
    errno = 0;
    value = strtoll(text, &end, 0);
    if (errno || end == text || *end) return -1;
    *out = value;
    return 0;
}

static int in_range(long long value, long long low, long long high)
{
    return value >= low && value <= high;
}

static int append_byte(fe2_buf *out, unsigned value)
{
    return fe2_buf_byte(out, (uint8_t)value);
}

static const char *operation_name(unsigned slot)
{
    static const char *names[32] = {
        "return_stream", "inline_curve", "line_segment", "bitmap_surface", "textured_quad",
        "polygon_value_sequence", "parameter_control", "adjusted_bitmap_surface",
        "adjusted_textured_quad", "vertex_pair_surface", "model_instance", "scaled_threshold",
        "inverse_scaled_threshold", "binary_expression", "nested_model_context",
        "position_duration_sound", "geometry_four", "geometry_six", "projected_vertex_text",
        "context_mask", "excluded_context_mask", "curve_batch", "four_vertex_lines",
        "vertex_distance_filter", "point_list", "update_basis_axis", "update_parameter",
        "conditional_nested_model", "load_basis_columns", "binary_expression_alternate",
        "configure_context", "geometry_pipeline"
    };
    return slot < 32 ? names[slot] : NULL;
}

static int operation_slot(const char *name, unsigned *slot)
{
    unsigned i;
    if (strcmp(name, "parameter_control") == 0) { *slot = 6; return 0; }
    for (i = 0; i < 32; ++i) {
        if (i == 6) continue;
        if (strcmp(name, operation_name(i)) == 0) { *slot = i; return 0; }
    }
    return -1;
}

int fe2_operand_count(unsigned token, const unsigned *words, size_t count, int *out)
{
    unsigned slot = token & 31u;
    if (slot == 6) {
        unsigned parameter = token >> 5;
        if (parameter == 0 || parameter == 0x7feu || parameter == 0x7ffu) { *out = 0; return 0; }
        *out = (parameter == 0x7fdu || (parameter & 0x100u)) ? 1 : 0;
        return 0;
    }
    if (slot == 14 || slot == 27) {
        if (!count) return -1;
        *out = (slot == 14 ? 1 : 2) + ((words[0] & 0x8000u) ? 2 : 0);
        return 0;
    }
    if (slot == 28) { *out = 1; return 0; }
    if (slot == 31) { *out = (int)(2u + (((token >> 4) & 0xfffeu) / 2u)); return 0; }
    if (slot == 5) {
        static const unsigned extra[8] = {0, 2, 1, 0, 1, 0, 1, 0};
        size_t index = 1;
        if (!count) return -1;
        while (index < count) {
            unsigned value_slot = (words[index] >> 9) & 7u;
            index += 1u + extra[value_slot];
            if (index > count) return -1;
            if (value_slot == 0) { *out = (int)index; return 0; }
        }
        return -1;
    }
    if (slot == 24) {
        size_t index;
        for (index = 1; index < count; ++index) {
            if ((words[index] & 255u) == 0x7fu) { *out = (int)index + 1; return 0; }
            if (index > 1 && (words[index] >> 8) == 0x7fu) return -1;
        }
        return -1;
    }
    if (slot == 26) {
        if (!count) return -1;
        *out = (words[0] & 0xff00u) ? 8 : 1;
        return 0;
    }
    {
        static const int fixed[32] = {
            0, 2, 1, 2, 3, -1, -1, 2, 3, 2, 3, 1, 1, 1, -1, 0,
            3, 5, 1, 1, 1, 0, 3, 2, -1, 0, -1, -1, -1, 1, 0, -1
        };
        if (fixed[slot] < 0) return -1;
        *out = fixed[slot];
        return 0;
    }
}

static int size_kind(unsigned word, char *kind, int *value)
{
    if (!(word & 0x80u)) {
        unsigned literal = ((word >> 7) | (word << 9)) & 0xffffu;
        if (literal & 0x8000u) { strcpy(kind, "fixed"); *value = (int)(literal & 0x7fffu); }
        else { strcpy(kind, "depth_scaled"); *value = (int)literal; }
        return 0;
    }
    {
        unsigned shifted = (word << 1) & 0xffffu;
        if (shifted & 0x80u) {
            strcpy(kind, "frame");
            *value = (int)(shifted & 0x7fu) - 0x40;
            return 0;
        }
        strcpy(kind, "object");
        *value = (int)((shifted & 0xfe7fu) + 0x72u);
    }
    return 0;
}

static void material_text(unsigned token, char *kind, unsigned *value, int *inherit)
{
    unsigned selector = token >> 4;
    *inherit = (selector & 0x10u) != 0;
    if ((selector & 0xf00u) == 0xe00u) {
        strcpy(kind, "table");
        *value = ((selector & 0xeu) << 3) | ((selector & 0xe0u) << 2);
    } else {
        strcpy(kind, "rgb12");
        *value = selector & 0xeeeu;
    }
}

typedef struct word_list {
    unsigned *values;
    size_t count, cap;
} word_list;

static int push_word(word_list *list, unsigned value)
{
    if (list->count == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 16;
        unsigned *grown = (unsigned *)realloc(list->values, cap * sizeof *grown);
        if (!grown) return -1;
        list->values = grown;
        list->cap = cap;
    }
    list->values[list->count++] = value;
    return 0;
}

static void free_words(word_list *list)
{
    free(list->values);
    memset(list, 0, sizeof *list);
}

static int parse_point_pairs(const char *source, word_list *words)
{
    char *copy = strdup(source), *pair, *save = NULL;
    if (!copy) return -1;
    for (pair = strtok_r(copy, "|", &save); pair; pair = strtok_r(NULL, "|", &save)) {
        char *comma = strchr(pair, ',');
        char *fields[2];
        unsigned values[2];
        int position;
        size_t index = words->count;
        if (!comma || strchr(comma + 1, ',')) { free(copy); return fe2_asset_fail("Point pair requires high-byte and low-byte vertices"); }
        *comma = 0;
        fields[0] = pair;
        fields[1] = comma + 1;
        for (position = 0; position < 2; ++position) {
            long long value;
            if (strcmp(fields[position], "end") == 0) {
                if (index == 0 && position == 0) { free(copy); return fe2_asset_fail("First high-byte vertex cannot be a terminator"); }
                value = 127;
            } else {
                if (parse_i64(fields[position], &value) || !in_range(value, -128, 127)) {
                    free(copy); return fe2_asset_fail("Point vertex exceeds signed byte range");
                }
                if (value == 127 && (position == 1 || index > 0)) {
                    free(copy); return fe2_asset_fail("Point sentinel must be written as end");
                }
            }
            values[position] = (unsigned)value & 255u;
        }
        if (push_word(words, (values[0] << 8) | values[1])) { free(copy); return -1; }
    }
    free(copy);
    return 0;
}

static int polygon_values(unsigned slot, const unsigned *encoded, long long *out, int *count)
{
    if (slot == 0 || slot == 5) { *count = 0; return 0; }
    if (slot == 1) {
        *count = 4;
        out[0] = encoded[1] >> 8; out[1] = encoded[1] & 255u;
        out[2] = encoded[2] >> 8; out[3] = encoded[2] & 255u;
        return 0;
    }
    if (slot == 2) { *count = 2; out[0] = encoded[0] & 255u; out[1] = encoded[1] & 255u; return 0; }
    if (slot == 3) { *count = 1; out[0] = encoded[0] & 255u; return 0; }
    if (slot == 4) { *count = 3; out[0] = encoded[0] & 255u; out[1] = encoded[1] >> 8; out[2] = encoded[1] & 255u; return 0; }
    if (slot == 6) { *count = 3; out[0] = encoded[0] & 255u; out[1] = encoded[1] >> 8; out[2] = encoded[1] & 127u; return 0; }
    return 1;
}

static int polygon_slot(const char *name, unsigned *slot, unsigned *width)
{
    static const char *names[] = {
        "end", "bezier", "line", "line_cont", "bezier_cont", "line_join", "circle", "adjacent_operator"
    };
    static const unsigned widths[] = {1, 3, 2, 1, 2, 1, 2, 1};
    unsigned i;
    if (strcmp(name, "join") == 0) name = "line_join";
    for (i = 0; i < 8; ++i) {
        if (strcmp(name, names[i]) == 0) { *slot = i; *width = widths[i]; return 0; }
    }
    return -1;
}

static int parse_polygon(const char *source, word_list *words)
{
    char *copy = strdup(source), *command, *save = NULL;
    size_t commands = 0, index = 0;
    if (!copy || !copy[0]) { free(copy); return fe2_asset_fail("Polygon command list is empty or has a trailing separator"); }
    if (copy[strlen(copy) - 1] == '|') { free(copy); return fe2_asset_fail("Polygon command list is empty or has a trailing separator"); }
    for (command = strtok_r(copy, "|", &save); command; command = strtok_r(NULL, "|", &save)) ++commands;
    free(copy);
    copy = strdup(source);
    if (!copy) return -1;
    for (command = strtok_r(copy, "|", &save); command; command = strtok_r(NULL, "|", &save), ++index) {
        char *colon = strchr(command, ':');
        char *at, *semantic, *encoded;
        unsigned slot, width, got = 0, values[3];
        long long supplied[8], expected[8];
        int supplied_count = 0, expected_count = 0, known;
        if (!colon) { free(copy); return fe2_asset_fail("Unknown polygon subcommand"); }
        *colon = 0;
        if (polygon_slot(command, &slot, &width)) { free(copy); return fe2_asset_fail("Unknown polygon subcommand"); }
        at = strchr(colon + 1, '@');
        if (!at) { free(copy); return fe2_asset_fail("Polygon subcommand has an invalid encoded word span"); }
        *at = 0;
        semantic = colon + 1;
        encoded = at + 1;
        while (got < 4) {
            char *comma = strchr(encoded, ',');
            long long value;
            if (comma) *comma = 0;
            if (parse_i64(encoded, &value) || !in_range(value, 0, 65535)) {
                free(copy); return fe2_asset_fail("Polygon subcommand has an invalid encoded word span");
            }
            if (got < 3) values[got] = (unsigned)value;
            ++got;
            if (!comma) break;
            encoded = comma + 1;
        }
        if (got != width) {
            free(copy); return fe2_asset_fail("Polygon subcommand has an invalid encoded word span");
        }
        if (((values[0] >> 9) & 7u) != slot) { free(copy); return fe2_asset_fail("Polygon subcommand name does not match encoded dispatch"); }
        known = polygon_values(slot, values, expected, &expected_count);
        if (semantic[0]) {
            char *item = semantic;
            while (item && supplied_count < 8) {
                char *comma = strchr(item, ',');
                long long value;
                if (comma) *comma = 0;
                if (parse_i64(item, &value)) { free(copy); return fe2_asset_fail("Polygon subcommand fields disagree with encoded words"); }
                supplied[supplied_count++] = value;
                item = comma ? comma + 1 : NULL;
            }
        }
        if (known == 0 && (supplied_count != expected_count || memcmp(supplied, expected, (size_t)supplied_count * sizeof *supplied) != 0)) {
            free(copy); return fe2_asset_fail("Polygon subcommand fields disagree with encoded words");
        }
        if (known != 0 && supplied_count) { free(copy); return fe2_asset_fail("Adjacent operator alias has no established fields"); }
        if (slot == 0 && index + 1 != commands) { free(copy); return fe2_asset_fail("Polygon end command must terminate its substream"); }
        for (got = 0; got < width; ++got) if (push_word(words, values[got])) { free(copy); return -1; }
    }
    if (!words->count || ((words->values[words->count - 1] >> 9) & 7u) != 0) {
        free(copy); return fe2_asset_fail("Polygon subcommand list has no end command");
    }
    free(copy);
    return 0;
}

int fe2_ship_record(const char *csv, const char *model, const char *names_path, fe2_buf *out)
{
    static const char header[] =
        "model,main_thrust,retro_thrust,gun_mount_flags,hull_mass,internal_capacity,price,preview_size,"
        "name_text,required_crew,missile_pylons,drive_options,combat_points,front_eye_forward,front_eye_up,"
        "rear_eye_forward,rear_eye_up,front_gun_forward,front_gun_up,rear_gun_forward,rear_gun_up,"
        "top_gun_forward,top_gun_up,bottom_gun_forward,bottom_gun_up";
    char *text = NULL, *names = NULL, *line;
    char **labels = NULL;
    size_t labels_n = 0, length = 0;
    int found = 0, status = -1;
    if (names_path) {
        char *cursor;
        names = fe2_read_text(names_path, &length);
        if (!names) return fe2_asset_fail("Could not read %s", names_path);
        cursor = names;
        while (*cursor) {
            char *end = strchr(cursor, '\n');
            char *tab, **grown;
            if (end) *end = 0;
            if (cursor[0] && cursor[strlen(cursor) - 1] == '\r') cursor[strlen(cursor) - 1] = 0;
            tab = strchr(cursor, '\t');
            if (tab) *tab = 0;
            grown = (char **)realloc(labels, (labels_n + 1) * sizeof *grown);
            if (!grown) goto done;
            labels = grown;
            labels[labels_n++] = cursor;
            if (!end) break;
            cursor = end + 1;
            if (!*cursor) break;
        }
    }
    text = strdup(csv);
    if (!text) goto done;
    {
        char *cursor = text;
        int header_row = 1;
        while (cursor && *cursor) {
        char *fields[25];
        unsigned words[12];
        int mounts[12], mount_count = 0, absent = 0, column, word_index, got = 0;
        char *cell;
        char *row_end = strchr(cursor, '\n');
        line = cursor;
        if (row_end) { *row_end = 0; cursor = row_end + 1; }
        else cursor = NULL;
        if (line[0] && line[strlen(line) - 1] == '\r') line[strlen(line) - 1] = 0;
        if (header_row) {
            header_row = 0;
            if (strcmp(line, header) != 0) { fe2_asset_fail("Unexpected ship parameter columns"); goto done; }
            continue;
        }
        if (!line[0]) { fe2_asset_fail("Invalid ship parameter row"); goto done; }
        cell = line;
        for (column = 0; column < 25; ++column) {
            char *comma = strchr(cell, ',');
            fields[column] = cell;
            if (comma) { *comma = 0; cell = comma + 1; }
            else cell = NULL;
            ++got;
            if (!comma) break;
        }
        if (got != 25 || cell) { fe2_asset_fail("Invalid ship parameter row"); goto done; }
        if (!fields[0][0]) { fe2_asset_fail("Ship model must be unique and nonempty"); goto done; }
        if (strcmp(fields[0], model) != 0) continue;
        for (word_index = 0; word_index < 12; ++word_index) {
            const char *cell = fields[word_index + 1];
            long long value;
            size_t name;
            int named = 0;
            if (!cell) { fe2_asset_fail("Invalid ship parameter row"); goto done; }
            if (word_index == 7 && names) {
                for (name = 0; name < labels_n; ++name) {
                    if (strcmp(labels[name], cell) == 0) {
                        value = (long long)(0x4000 + name);
                        named = 1;
                        break;
                    }
                }
            }
            if (!named && (parse_i64(cell, &value) || !in_range(value, 0, 65535))) {
                fe2_asset_fail("Ship parameter exceeds a word"); goto done;
            }
            words[word_index] = (unsigned)value;
        }
        for (column = 13; column < 25; ++column) {
            long long value;
            if (!fields[column] || !fields[column][0]) { absent = 1; continue; }
            if (absent || parse_i64(fields[column], &value) || !in_range(value, -32768, 32767)) {
                fe2_asset_fail("Omitted mount coordinates must be a trailing suffix"); goto done;
            }
            mounts[mount_count++] = (int)value;
        }
        if (mount_count != 4 && mount_count != 6 && mount_count != 8 && mount_count != 10 && mount_count != 12) {
            fe2_asset_fail("Expected paired eye/mount coordinates, including both eye positions"); goto done;
        }
        for (word_index = 0; word_index < 12; ++word_index) if (fe2_buf_u16(out, words[word_index])) goto done;
        for (column = 0; column < mount_count; ++column) {
            unsigned packed = (unsigned)(int16_t)mounts[column];
            if (fe2_buf_u16(out, packed)) goto done;
        }
        found = 1;
        break;
        }
    }
    if (!found) { fe2_asset_fail("Unknown ship parameter model"); goto done; }
    status = 0;
done:
    free(labels);
    free(names);
    free(text);
    return status;
}

static int hex_payload(const char *text, fe2_buf *out)
{
    const char *cursor = text;
    if (strcmp(text, "-") == 0) return 0;
    while (*cursor) {
        unsigned value;
        while (*cursor == ' ' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t') ++cursor;
        if (!*cursor) break;
        if (!isxdigit((unsigned char)cursor[0]) || !isxdigit((unsigned char)cursor[1]))
            return fe2_asset_fail("Geometry pipeline header or declared payload length changed");
        value = (unsigned)(isdigit((unsigned char)cursor[0]) ? cursor[0] - '0' : tolower((unsigned char)cursor[0]) - 'a' + 10);
        value = (value << 4) | (unsigned)(isdigit((unsigned char)cursor[1]) ? cursor[1] - '0' : tolower((unsigned char)cursor[1]) - 'a' + 10);
        if (append_byte(out, value)) return -1;
        cursor += 2;
    }
    return 0;
}

static int compile_geometry_line(char *line, int *curve, int *previous, fe2_buf *out)
{
    char *tokens[40];
    size_t count = 0;
    char *cursor = line;
    while (*cursor && count < 40) {
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (!*cursor) break;
        tokens[count++] = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t') ++cursor;
        if (*cursor) *cursor++ = 0;
    }
    if (!count) return 0;
    if (!strcmp(tokens[0], "vector_phase") || !strcmp(tokens[0], "begin_curve") ||
        !strcmp(tokens[0], "isolated_point") || !strcmp(tokens[0], "curve_start") ||
        !strcmp(tokens[0], "curve_point")) {
        const char *tag = NULL, *xyz = NULL, *phase = NULL, *selector = NULL, *parameter = NULL;
        long long point[3], tag_value = 0, extra = 0;
        size_t i;
        int point_count = 0;
        for (i = 1; i < count; ++i) {
            char *eq = strchr(tokens[i], '=');
            if (!eq) return fe2_asset_fail("Invalid geometry byte-record fields");
            *eq = 0;
            if ((strcmp(tokens[i], "tag") == 0 && tag) || (strcmp(tokens[i], "xyz") == 0 && xyz) ||
                (strcmp(tokens[i], "phase") == 0 && phase) || (strcmp(tokens[i], "selector") == 0 && selector) ||
                (strcmp(tokens[i], "parameter") == 0 && parameter))
                return fe2_asset_fail("Duplicate geometry field");
            if (strcmp(tokens[i], "tag") == 0) tag = eq + 1;
            else if (strcmp(tokens[i], "xyz") == 0) xyz = eq + 1;
            else if (strcmp(tokens[i], "phase") == 0) phase = eq + 1;
            else if (strcmp(tokens[i], "selector") == 0) selector = eq + 1;
            else if (strcmp(tokens[i], "parameter") == 0) parameter = eq + 1;
            else return fe2_asset_fail("Invalid geometry byte-record fields");
        }
        if (xyz) {
            char *item = (char *)xyz, *comma;
            for (point_count = 0; point_count < 3; ++point_count) {
                comma = strchr(item, ',');
                if (comma) *comma = 0;
                if (parse_i64(item, &point[point_count]) || !in_range(point[point_count], -128, 127))
                    return fe2_asset_fail("Geometry vector needs three signed8-bit components");
                if (!comma) { ++point_count; break; }
                item = comma + 1;
            }
            if (point_count != 3 || (item && strchr(item, ',')))
                return fe2_asset_fail("Geometry vector needs three signed8-bit components");
        }
        if (!strcmp(tokens[0], "vector_phase")) {
            if (*curve || !tag || !xyz || !phase || parse_i64(tag, &tag_value) || parse_i64(phase, &extra) ||
                !in_range(tag_value, 128, 255) || !in_range(extra, 0, 255))
                return fe2_asset_fail("Invalid negative-tag phase record");
            if (append_byte(out, (unsigned)tag_value) || append_byte(out, (unsigned)point[0]) ||
                append_byte(out, (unsigned)point[1]) || append_byte(out, (unsigned)point[2]) ||
                append_byte(out, (unsigned)extra)) return -1;
        } else if (!strcmp(tokens[0], "begin_curve")) {
            if (*curve || !tag || !selector || xyz || parse_i64(tag, &tag_value) || parse_i64(selector, &extra) ||
                !in_range(tag_value, 1, 127) || !in_range(extra, -128, 127))
                return fe2_asset_fail("Invalid positive-tag curve header");
            if (append_byte(out, (unsigned)tag_value) || append_byte(out, (unsigned)extra)) return -1;
            *curve = 1;
        } else if (!strcmp(tokens[0], "isolated_point")) {
            if (*curve != 1 || !xyz || !parameter || parse_i64(parameter, &extra) || !in_range(extra, 0, 255))
                return fe2_asset_fail("Invalid isolated geometry point");
            if (append_byte(out, (unsigned)point[0]) || append_byte(out, (unsigned)point[1]) ||
                append_byte(out, (unsigned)point[2]) || append_byte(out, 0) || append_byte(out, (unsigned)extra))
                return -1;
        } else if (!strcmp(tokens[0], "curve_start")) {
            if (*curve != 1 || !xyz) return fe2_asset_fail("Curve start must follow its header/isolated points");
            if (append_byte(out, (unsigned)point[0]) || append_byte(out, (unsigned)point[1]) ||
                append_byte(out, (unsigned)point[2])) return -1;
            *curve = 2;
        } else {
            if ((*curve != 2 && *curve != 3) || !xyz || point[0] == 0)
                return fe2_asset_fail("Additional curve point needs nonzero X");
            if (append_byte(out, (unsigned)point[0]) || append_byte(out, (unsigned)point[1]) ||
                append_byte(out, (unsigned)point[2])) return -1;
            *curve = 3;
        }
    } else if (!strcmp(tokens[0], "end_curve")) {
        if (*curve != 3 || count != 1) return fe2_asset_fail("Curve needs an additional point before its terminator");
        if (append_byte(out, 0)) return -1;
        *curve = 0;
    } else if (!strcmp(tokens[0], "end_records")) {
        if (*curve || count != 1) return fe2_asset_fail("Cannot terminate incomplete geometry curve");
        if (append_byte(out, 0)) return -1;
    } else if (!strcmp(tokens[0], "padding")) {
        long long value;
        if (strcmp(tokens[0], "padding") || *previous != 1 || (out->count % 2) != 1 || count != 2 ||
            parse_i64(tokens[1], &value) || !in_range(value, 0, 255))
            return fe2_asset_fail("Geometry padding must align an odd records-end cursor");
        if (append_byte(out, (unsigned)value)) return -1;
    } else if (!strcmp(tokens[0], "raw")) {
        size_t i;
        if (*curve) return fe2_asset_fail("Raw fragment cannot split a named curve");
        if (count < 2) return fe2_asset_fail("Geometry raw byte outside8-bit range");
        for (i = 1; i < count; ++i) {
            long long value;
            if (parse_i64(tokens[i], &value) || !in_range(value, 0, 255))
                return fe2_asset_fail("Geometry raw byte outside8-bit range");
            if (append_byte(out, (unsigned)value)) return -1;
        }
    } else if (!strcmp(tokens[0], "primary_words") || !strcmp(tokens[0], "selection_words") ||
               !strcmp(tokens[0], "secondary_words")) {
        size_t expected = strcmp(tokens[0], "selection_words") ? 4 : 29;
        size_t i;
        if (count != expected + 1) return fe2_asset_fail("Geometry word block count or width changed");
        for (i = 1; i < count; ++i) {
            long long value;
            if (parse_i64(tokens[i], &value) || !in_range(value, 0, 65535) || fe2_buf_u16(out, (unsigned)value))
                return fe2_asset_fail("Geometry word block count or width changed");
        }
    } else if (!strcmp(tokens[0], "layer")) {
        long long scale, palette;
        if (count != 3 || parse_i64(tokens[1], &scale) || parse_i64(tokens[2], &palette) ||
            !in_range(scale, 1, 65535) || !in_range(palette, 0, 65535))
            return fe2_asset_fail("Geometry layer needs nonzero scale and palette words");
        if (fe2_buf_u16(out, (unsigned)scale) || fe2_buf_u16(out, (unsigned)palette)) return -1;
    } else if (!strcmp(tokens[0], "layer_base")) {
        long long value;
        if (count != 2 || parse_i64(tokens[1], &value) || !in_range(value, 0, 65535) || fe2_buf_u16(out, (unsigned)value))
            return fe2_asset_fail("Geometry layer base needs one word");
    } else if (!strcmp(tokens[0], "end_layers")) {
        if (count != 1 || fe2_buf_u16(out, 0)) return fe2_asset_fail("Layer terminator has no operands");
    } else return fe2_asset_fail("Unknown geometry source record");
    *previous = !strcmp(tokens[0], "end_records") ? 1 : 2;
    return 0;
}

int fe2_compile_geometry(const char *text, fe2_buf *out)
{
    char *copy = strdup(text), *line, *save = NULL;
    int curve = 0, previous = 0;
    if (!copy) return -1;
    for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *comment = strchr(line, '#');
        if (line[0] && line[strlen(line) - 1] == '\r') line[strlen(line) - 1] = 0;
        if (comment) *comment = 0;
        if (compile_geometry_line(line, &curve, &previous, out)) { free(copy); return -1; }
    }
    free(copy);
    if (curve) return fe2_asset_fail("Unterminated geometry curve");
    return 0;
}

static int contact_number(const char *text, unsigned maximum, unsigned *out)
{
    long long value;
    if (parse_i64(text, &value) || !in_range(value, 0, maximum)) return -1;
    *out = (unsigned)value;
    return 0;
}

int fe2_compile_contact(const char *text, fe2_buf *out)
{
    char *copy = strdup(text), *line, *save = NULL;
    int ended = 0, number = 0;
    if (!copy) return -1;
    for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save), ++number) {
        char *comment, *tokens[16], *cursor;
        size_t count = 0;
        if (line[0] && line[strlen(line) - 1] == '\r') line[strlen(line) - 1] = 0;
        comment = strchr(line, '#');
        if (comment) *comment = 0;
        cursor = line;
        while (*cursor && count < 16) {
            while (*cursor == ' ' || *cursor == '\t') ++cursor;
            if (!*cursor) break;
            tokens[count++] = cursor;
            while (*cursor && *cursor != ' ' && *cursor != '\t') ++cursor;
            if (*cursor) *cursor++ = 0;
        }
        if (!count) continue;
        if (ended) { free(copy); return fe2_asset_fail("Contact source line %d: Record after contact terminator", number); }
        if (count == 1 && strcmp(tokens[0], "end") == 0) {
            if (append_byte(out, 0)) { free(copy); return -1; }
            ended = 1;
            continue;
        }
        {
            const char *op = tokens[0];
            unsigned mask, tag = 0, result = 0, field = 0, shift = 0, model = 0, transform = 0, vertex = 0;
            const char *tag_text = NULL, *result_text = NULL, *field_text = NULL, *shift_text = NULL;
            const char *model_text = NULL, *transform_text = NULL, *vertex_text = NULL, *refs = NULL;
            size_t i;
            if (strcmp(op, "planes_all_le") == 0) mask = 0;
            else if (strcmp(op, "planes_all_gt") == 0) mask = 0x40;
            else if (strcmp(op, "child") == 0) mask = 0x80;
            else if (strcmp(op, "result_planes_all_le") == 0) mask = 0xc0;
            else { free(copy); return fe2_asset_fail("Contact source line %d: Unknown contact operation", number); }
            for (i = 1; i < count; ++i) {
                char *eq = strchr(tokens[i], '=');
                const char **target = NULL;
                if (!eq) { free(copy); return fe2_asset_fail("Contact source line %d: Incorrect contact operands", number); }
                *eq = 0;
                if (strcmp(tokens[i], "tag") == 0) target = &tag_text;
                else if (strcmp(tokens[i], "result") == 0) target = &result_text;
                else if (strcmp(tokens[i], "field") == 0) target = &field_text;
                else if (strcmp(tokens[i], "shift") == 0) target = &shift_text;
                else if (strcmp(tokens[i], "model") == 0) target = &model_text;
                else if (strcmp(tokens[i], "transform") == 0) target = &transform_text;
                else if (strcmp(tokens[i], "vertex") == 0) target = &vertex_text;
                else if (strcmp(tokens[i], "refs") == 0) target = &refs;
                if (!target || *target) { free(copy); return fe2_asset_fail("Contact source line %d: Duplicate contact operand", number); }
                *target = eq + 1;
            }
            if (!tag_text || contact_number(tag_text, 255, &tag) || !tag || (tag & 0xc0u) != mask ||
                !result_text || contact_number(result_text, 255, &result)) {
                free(copy); return fe2_asset_fail("Contact source line %d: Tag does not match contact operation", number);
            }
            if (((tag & 0x20u) != 0) != (field_text && shift_text)) {
                free(copy); return fe2_asset_fail("Contact source line %d: Incorrect contact operands", number);
            }
            if (strcmp(op, "child") == 0) {
                if (!model_text || !transform_text || !vertex_text || refs) {
                    free(copy); return fe2_asset_fail("Contact source line %d: Incorrect contact operands", number);
                }
                if (contact_number(model_text, 65535, &model) || contact_number(transform_text, 255, &transform) ||
                    contact_number(vertex_text, 255, &vertex)) {
                    free(copy); return fe2_asset_fail("Contact source line %d: Contact operand out of range", number);
                }
            } else if (!refs || model_text) {
                free(copy); return fe2_asset_fail("Contact source line %d: Incorrect contact operands", number);
            }
            if ((tag & 0x20u) && (contact_number(field_text, 255, &field) || contact_number(shift_text, 255, &shift))) {
                free(copy); return fe2_asset_fail("Contact source line %d: Contact operand out of range", number);
            }
            if (append_byte(out, tag) || append_byte(out, result)) { free(copy); return -1; }
            if ((tag & 0x20u) && (append_byte(out, field) || append_byte(out, shift))) { free(copy); return -1; }
            if (strcmp(op, "child") == 0) {
                if (append_byte(out, model >> 8) || append_byte(out, model) || append_byte(out, transform) ||
                    append_byte(out, vertex)) { free(copy); return -1; }
            } else if (strcmp(refs, "-") != 0) {
                char *item = (char *)refs;
                while (*item) {
                    char *comma = strchr(item, ',');
                    unsigned value;
                    if (comma) *comma = 0;
                    if (contact_number(item, 255, &value) || !value) {
                        free(copy); return fe2_asset_fail("Contact source line %d: Zero plane reference is a list terminator", number);
                    }
                    if (append_byte(out, value)) { free(copy); return -1; }
                    if (!comma) break;
                    item = comma + 1;
                }
                if (append_byte(out, 0)) { free(copy); return -1; }
            } else if (append_byte(out, 0)) { free(copy); return -1; }
        }
    }
    free(copy);
    if (!ended) return fe2_asset_fail("Missing contact terminator");
    return 0;
}

int fe2_compile_offsets(const char *text, fe2_buf *out)
{
    char *copy = strdup(text), *line, *save = NULL;
    int number = 0, any = 0;
    if (!copy) return -1;
    for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save), ++number) {
        char *comment, *tokens[8], *cursor;
        size_t count = 0;
        if (line[0] && line[strlen(line) - 1] == '\r') line[strlen(line) - 1] = 0;
        comment = strchr(line, '#');
        if (comment) *comment = 0;
        cursor = line;
        while (*cursor && count < 8) {
            while (*cursor == ' ' || *cursor == '\t') ++cursor;
            if (!*cursor) break;
            tokens[count++] = cursor;
            while (*cursor && *cursor != ' ' && *cursor != '\t') ++cursor;
            if (*cursor) *cursor++ = 0;
        }
        if (!count) continue;
        if (strcmp(tokens[0], "basis_offset") && strcmp(tokens[0], "stop")) {
            free(copy); return fe2_asset_fail("Unknown contact offset operation on line %d", number);
        }
        {
            const char *selector_text = NULL, *basis_text = NULL, *negate = NULL, *length_text = NULL, *unused = NULL;
            size_t i;
            for (i = 1; i < count; ++i) {
                char *eq = strchr(tokens[i], '=');
                const char **target = NULL;
                if (!eq) { free(copy); return fe2_asset_fail("Incorrect contact offset fields"); }
                *eq = 0;
                if (strcmp(tokens[i], "selector") == 0) target = &selector_text;
                else if (strcmp(tokens[i], "basis_word_offset") == 0) target = &basis_text;
                else if (strcmp(tokens[i], "negate") == 0) target = &negate;
                else if (strcmp(tokens[i], "length") == 0) target = &length_text;
                else if (strcmp(tokens[i], "unused_word") == 0) target = &unused;
                if (!target || *target) { free(copy); return fe2_asset_fail("Duplicate contact offset field"); }
                *target = eq + 1;
            }
            if (strcmp(tokens[0], "stop") == 0) {
                long long value;
                if (any || !unused || selector_text || parse_i64(unused, &value) || !in_range(value, 0, 65535)) {
                    free(copy); return fe2_asset_fail("Backward-walk stop must be first in physical storage");
                }
                if (fe2_buf_u16(out, (unsigned)value) || fe2_buf_u16(out, 0)) { free(copy); return -1; }
            } else {
                long long selector, basis, length;
                char expect[8];
                if (!selector_text || !basis_text || !negate || !length_text || unused ||
                    parse_i64(selector_text, &selector) || parse_i64(basis_text, &basis) || parse_i64(length_text, &length) ||
                    !in_range(selector, 0, 65535) || basis != (selector & 6) || !in_range(length, -32768, 32767) || length == 0) {
                    free(copy); return fe2_asset_fail("Contact offset fields disagree with encoded selector/length");
                }
                snprintf(expect, sizeof expect, "%s", (selector & 1) ? "true" : "false");
                if (strcmp(negate, expect) != 0) {
                    free(copy); return fe2_asset_fail("Contact offset fields disagree with encoded selector/length");
                }
                if (fe2_buf_u16(out, (unsigned)selector) || fe2_buf_u16(out, (unsigned)(int16_t)length)) {
                    free(copy); return -1;
                }
            }
            any = 1;
        }
    }
    free(copy);
    if (!any) return fe2_asset_fail("Contact offsets require at least one pair");
    return 0;
}

static const char *field_value(char **keys, char **values, size_t count, const char *key)
{
    size_t i;
    for (i = 0; i < count; ++i) if (strcmp(keys[i], key) == 0) return values[i];
    return NULL;
}

static int field_ok(const char *key, int geometry)
{
    static const char *allowed[] = {
        "token", "words", "truncated", "commands", "normal_index", "skip_bytes", "skip_if_z_clipped",
        "header", "parameter", "points", "size", "material", "inherit_material", "payload", "file"
    };
    size_t i;
    if (!geometry && (strcmp(key, "payload") == 0 || strcmp(key, "file") == 0)) return 0;
    for (i = 0; i < sizeof allowed / sizeof *allowed; ++i) if (strcmp(key, allowed[i]) == 0) return 1;
    return 0;
}

int fe2_compile_program(const char *text, const char *base, const char *names_path, fe2_buf *out)
{
    char *copy = strdup(text), *line, *save = NULL;
    int number = 0;
    if (!copy) return -1;
    for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save), ++number) {
        char *comment, *cursor, *tokens[24];
        size_t token_count = 0;
        if (line[0] && line[strlen(line) - 1] == '\r') line[strlen(line) - 1] = 0;
        comment = strchr(line, '#');
        if (comment) *comment = 0;
        cursor = line;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (!*cursor) continue;
        while (*cursor && token_count < 24) {
            while (*cursor == ' ' || *cursor == '\t') ++cursor;
            if (!*cursor) break;
            tokens[token_count++] = cursor;
            while (*cursor && *cursor != ' ' && *cursor != '\t') ++cursor;
            if (*cursor) *cursor++ = 0;
        }
        if (!token_count || token_count == 24) { free(copy); return fe2_asset_fail("Invalid renderer fields on line %d", number); }
        if (strcmp(tokens[0], "ship_parameters") == 0) {
            const char *file = NULL, *model = NULL;
            char path[4096], *csv;
            size_t i;
            if (token_count != 3 || !base) { free(copy); return fe2_asset_fail("Ship parameters require file, model and an asset reader"); }
            for (i = 1; i < 3; ++i) {
                char *eq = strchr(tokens[i], '=');
                if (!eq) { free(copy); return fe2_asset_fail("Ship parameters require file, model and an asset reader"); }
                *eq = 0;
                if (strcmp(tokens[i], "file") == 0) file = eq + 1;
                else if (strcmp(tokens[i], "model") == 0) model = eq + 1;
            }
            if (!file || !model || fe2_path_join(path, sizeof path, base, file)) {
                free(copy); return fe2_asset_fail("Ship parameters require file, model and an asset reader");
            }
            csv = fe2_read_text(path, NULL);
            if (!csv || fe2_ship_record(csv, model, names_path, out)) { free(csv); free(copy); return -1; }
            free(csv);
            continue;
        }
        if (strcmp(tokens[0], "raw") == 0) {
            size_t i;
            if (token_count < 2) { free(copy); return fe2_asset_fail("Invalid raw bytes on line %d", number); }
            for (i = 1; i < token_count; ++i) {
                char *item = tokens[i];
                while (item) {
                    char *comma = strchr(item, ',');
                    long long value;
                    if (comma) *comma = 0;
                    if (!item[0]) { item = comma ? comma + 1 : NULL; continue; }
                    if (parse_i64(item, &value) || !in_range(value, 0, 255) || append_byte(out, (unsigned)value)) {
                        free(copy); return fe2_asset_fail("Invalid raw bytes on line %d", number);
                    }
                    item = comma ? comma + 1 : NULL;
                }
            }
            continue;
        }
        {
            int geometry = strcmp(tokens[0], "geometry_pipeline") == 0;
            char *keys[16], *values[16];
            size_t fields = 0, i;
            const char *truncated, *token_text, *words_text;
            unsigned slot = 0, token = 0;
            word_list words;
            long long value;
            memset(&words, 0, sizeof words);
            for (i = 1; i < token_count; ++i) {
                char *eq = strchr(tokens[i], '=');
                size_t k;
                if (!eq) { free(copy); return fe2_asset_fail("Invalid renderer fields on line %d", number); }
                *eq = 0;
                if (!field_ok(tokens[i], geometry)) {
                    free(copy); return fe2_asset_fail("Invalid renderer fields on line %d", number);
                }
                for (k = 0; k < fields; ++k) if (strcmp(keys[k], tokens[i]) == 0) {
                    free(copy); return fe2_asset_fail("Invalid renderer fields on line %d", number);
                }
                keys[fields] = tokens[i];
                values[fields] = eq + 1;
                ++fields;
            }
            truncated = field_value(keys, values, fields, "truncated");
            if (truncated && strcmp(truncated, "true") && strcmp(truncated, "false")) {
                free(copy); return fe2_asset_fail("Renderer truncated marker must be true or false");
            }
            if (!truncated) truncated = "false";
            if (operation_slot(tokens[0], &slot)) { free(copy); return fe2_asset_fail("Operation does not match dispatch token"); }
            {
                int has_commands = field_value(keys, values, fields, "commands") != NULL;
                int has_header = field_value(keys, values, fields, "header") != NULL;
                int has_points = field_value(keys, values, fields, "points") != NULL;
                int has_size = field_value(keys, values, fields, "size") != NULL;
                int has_file = field_value(keys, values, fields, "file") != NULL;
                int has_payload = field_value(keys, values, fields, "payload") != NULL;
                int has_words = field_value(keys, values, fields, "words") != NULL;
                int has_token = field_value(keys, values, fields, "token") != NULL;
                int has_parameter = field_value(keys, values, fields, "parameter") != NULL;
                int has_material = field_value(keys, values, fields, "material") != NULL;
                int has_inherit = field_value(keys, values, fields, "inherit_material") != NULL;
                int has_normal = field_value(keys, values, fields, "normal_index") != NULL;
                int has_skip = field_value(keys, values, fields, "skip_bytes") != NULL;
                int has_clip = field_value(keys, values, fields, "skip_if_z_clipped") != NULL;
                int ok = 0;
                if (geometry) ok = has_token && has_words && !has_commands && !has_header && !has_points &&
                    ((has_file && !has_payload) || (!has_file && has_payload));
                else if (slot == 5) ok = has_token && !has_file && !has_payload && !has_points && (
                    (has_words && !has_commands && !has_header && !has_normal && !has_skip && !has_clip) ||
                    (has_words && has_commands && !has_header && !has_normal && !has_skip && !has_clip) ||
                    (!has_words && has_commands && has_header && has_normal && has_skip));
                else if (slot == 24) ok = has_token && !has_file && !has_payload && !has_commands && !has_header && (
                    (has_words && !has_points && !has_parameter && !has_size && !has_material && !has_inherit) ||
                    (!has_words && has_points && has_parameter && !has_size && !has_material && !has_inherit) ||
                    (!has_words && has_points && has_size && has_material && has_inherit && !has_parameter));
                else ok = has_token && has_words && !has_commands && !has_header && !has_points && !has_file && !has_payload;
                if (!ok) {
                    free_words(&words); free(copy); return fe2_asset_fail("Missing renderer fields on line %d", number);
                }
            }
            token_text = field_value(keys, values, fields, "token");
            if (parse_i64(token_text, &value) || !in_range(value, 0, 65535)) {
                free(copy); return fe2_asset_fail("Renderer word exceeds 16 bits");
            }
            token = (unsigned)value;
            if ((token & 31u) != slot) { free(copy); return fe2_asset_fail("Operation does not match dispatch token"); }
            if (field_value(keys, values, fields, "points")) {
                const char *parameter = field_value(keys, values, fields, "size");
                unsigned first;
                if (parameter) {
                    char *at = strchr(parameter, '@');
                    char kind[16], supplied_kind[32];
                    int decoded, supplied;
                    long long word;
                    if (!at) { free(copy); return fe2_asset_fail("Point size must retain its encoded word after @"); }
                    *at = 0;
                    if (parse_i64(at + 1, &word) || !in_range(word, 0, 65535) || size_kind((unsigned)word, kind, &decoded)) {
                        free(copy); return fe2_asset_fail("Point size fields disagree with its encoded operand");
                    }
                    if (sscanf(parameter, "%31[^:]:%d", supplied_kind, &supplied) != 2 || strcmp(supplied_kind, kind) || supplied != decoded) {
                        free(copy); return fe2_asset_fail("Point size fields disagree with its encoded operand");
                    }
                    first = (unsigned)word;
                } else if (parse_i64(field_value(keys, values, fields, "parameter"), &value) || !in_range(value, 0, 65535)) {
                    free(copy); return fe2_asset_fail("Renderer word exceeds 16 bits");
                } else first = (unsigned)value;
                if (push_word(&words, first) || parse_point_pairs(field_value(keys, values, fields, "points"), &words)) {
                    free_words(&words); free(copy); return -1;
                }
                if (field_value(keys, values, fields, "material")) {
                    char kind[16], supplied_kind[32];
                    unsigned expected;
                    int inherit, supplied_value;
                    const char *material = field_value(keys, values, fields, "material");
                    material_text(token, kind, &expected, &inherit);
                    if (sscanf(material, "%31[^:]:%i", supplied_kind, &supplied_value) != 2 || strcmp(supplied_kind, kind) ||
                        (unsigned)supplied_value != expected ||
                        strcmp(field_value(keys, values, fields, "inherit_material"), inherit ? "true" : "false") != 0) {
                        free_words(&words); free(copy); return fe2_asset_fail("Point material fields disagree with its dispatch token");
                    }
                }
            } else if (field_value(keys, values, fields, "header")) {
                long long header, normal_index, skip_bytes;
                const char *clip = field_value(keys, values, fields, "skip_if_z_clipped");
                if (parse_i64(field_value(keys, values, fields, "header"), &header) ||
                    parse_i64(field_value(keys, values, fields, "normal_index"), &normal_index) ||
                    parse_i64(field_value(keys, values, fields, "skip_bytes"), &skip_bytes) ||
                    !in_range(header, 0, 65535) || normal_index != (header & 0x7f) || skip_bytes != (header >> 8) ||
                    (clip && ((strcmp(clip, "true") == 0) != ((header & 0x80) != 0) || (strcmp(clip, "true") && strcmp(clip, "false"))))) {
                    free_words(&words); free(copy); return fe2_asset_fail("Polygon header fields disagree with its encoded word");
                }
                if (push_word(&words, (unsigned)header)) { free_words(&words); free(copy); return -1; }
            } else {
                words_text = field_value(keys, values, fields, "words");
                if (strcmp(words_text, "-") != 0) {
                    char *items = strdup(words_text), *item, *word_save = NULL;
                    if (!items) { free(copy); return -1; }
                    for (item = strtok_r(items, ",", &word_save); item; item = strtok_r(NULL, ",", &word_save)) {
                        if (parse_i64(item, &value) || !in_range(value, 0, 65535) || push_word(&words, (unsigned)value)) {
                            free(items); free_words(&words); free(copy);
                            return fe2_asset_fail("Renderer word exceeds 16 bits");
                        }
                    }
                    free(items);
                }
            }
            if (field_value(keys, values, fields, "commands")) {
                if (slot != 5 || words.count != 1 || parse_polygon(field_value(keys, values, fields, "commands"), &words)) {
                    free_words(&words); free(copy);
                    return fe2_asset_fail("Named polygon commands require one initial parameter word");
                }
            }
            if (geometry) {
                fe2_buf payload;
                unsigned declared;
                memset(&payload, 0, sizeof payload);
                if (words.count != 2) {
                    free_words(&words); free(copy);
                    return fe2_asset_fail("Geometry pipeline header or declared payload length changed");
                }
                if (field_value(keys, values, fields, "file")) {
                    char path[4096], *body;
                    if (!base || fe2_path_join(path, sizeof path, base, field_value(keys, values, fields, "file"))) {
                        free_words(&words); free(copy); return -1;
                    }
                    body = fe2_read_text(path, NULL);
                    if (!body || fe2_compile_geometry(body, &payload)) {
                        free(body); fe2_buf_free(&payload); free_words(&words); free(copy); return -1;
                    }
                    free(body);
                } else if (hex_payload(field_value(keys, values, fields, "payload"), &payload)) {
                    fe2_buf_free(&payload); free_words(&words); free(copy); return -1;
                }
                declared = (token >> 4) & 0xfffeu;
                if ((strcmp(truncated, "false") == 0 && payload.count != declared) ||
                    (strcmp(truncated, "true") == 0 && payload.count >= declared)) {
                    fe2_buf_free(&payload); free_words(&words); free(copy);
                    return fe2_asset_fail("Geometry pipeline header or declared payload length changed");
                }
                if (fe2_buf_u16(out, token) || fe2_buf_u16(out, words.values[0]) || fe2_buf_u16(out, words.values[1]) ||
                    fe2_buf_add(out, payload.data, payload.count)) {
                    fe2_buf_free(&payload); free_words(&words); free(copy); return -1;
                }
                fe2_buf_free(&payload);
                free_words(&words);
                continue;
            }
            {
                int count = 0;
                if (fe2_operand_count(token, words.values, words.count, &count) ||
                    ((size_t)count != words.count && !(strcmp(truncated, "true") == 0 && words.count < (size_t)count))) {
                    free_words(&words); free(copy);
                    return fe2_asset_fail("Unsupported token or incorrect renderer operand count");
                }
                if (fe2_buf_u16(out, token)) { free_words(&words); free(copy); return -1; }
                for (i = 0; i < words.count; ++i) if (fe2_buf_u16(out, words.values[i])) {
                    free_words(&words); free(copy); return -1;
                }
            }
            free_words(&words);
        }
    }
    free(copy);
    return 0;
}
