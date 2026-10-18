/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "asset_program.h"

#include "asset_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fe2_text {
    char *data;
    size_t count, cap;
} fe2_text;

static int text_add(fe2_text *text, const char *bytes, size_t count)
{
    if (text->count + count + 1 > text->cap) {
        size_t cap = text->cap ? text->cap : 256;
        char *grown;
        while (cap < text->count + count + 1) cap *= 2;
        grown = (char *)realloc(text->data, cap);
        if (!grown) return -1;
        text->data = grown;
        text->cap = cap;
    }
    if (count) memcpy(text->data + text->count, bytes, count);
    text->count += count;
    text->data[text->count] = 0;
    return 0;
}

static int text_str(fe2_text *text, const char *value)
{
    return text_add(text, value, strlen(value));
}

static unsigned read_u16(const uint8_t *data)
{
    return data[0] | ((unsigned)data[1] << 8);
}

static int read_s16(const uint8_t *data)
{
    unsigned value = read_u16(data);
    return value & 0x8000u ? (int)value - 65536 : (int)value;
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
    return slot == 6 ? "parameter_control" : names[slot];
}

static void size_text(unsigned word, char *out, size_t cap)
{
    char kind[16];
    int value;
    if (!(word & 0x80u)) {
        unsigned literal = ((word >> 7) | (word << 9)) & 0xffffu;
        if (literal & 0x8000u) snprintf(kind, sizeof kind, "fixed"), value = (int)(literal & 0x7fffu);
        else snprintf(kind, sizeof kind, "depth_scaled"), value = (int)literal;
    } else {
        unsigned shifted = (word << 1) & 0xffffu;
        if (shifted & 0x80u) snprintf(kind, sizeof kind, "frame"), value = (int)(shifted & 0x7fu) - 0x40;
        else snprintf(kind, sizeof kind, "object"), value = (int)((shifted & 0xfe7fu) + 0x72u);
    }
    snprintf(out, cap, "%s:%d@0x%04x", kind, value, word);
}

static int format_points(const unsigned *words, size_t count, fe2_text *out)
{
    size_t index;
    for (index = 0; index < count; ++index) {
        unsigned high = words[index] >> 8, low = words[index] & 255u;
        char piece[32];
        int values[2] = {(int)high, (int)low};
        int position;
        if (index && text_str(out, "|")) return -1;
        for (position = 0; position < 2; ++position) {
            int marker = values[position] == 0x7f && (position == 1 || index > 0);
            if (position && text_str(out, ",")) return -1;
            if (marker) snprintf(piece, sizeof piece, "end");
            else snprintf(piece, sizeof piece, "%d", values[position] < 128 ? values[position] : values[position] - 256);
            if (text_str(out, piece)) return -1;
        }
    }
    return 0;
}

static int polygon_semantic(unsigned slot, const unsigned *encoded, fe2_text *out)
{
    char piece[64];
    if (slot == 0 || slot == 5) return 0;
    if (slot == 1) snprintf(piece, sizeof piece, "%u,%u,%u,%u", encoded[1] >> 8, encoded[1] & 255u, encoded[2] >> 8, encoded[2] & 255u);
    else if (slot == 2) snprintf(piece, sizeof piece, "%u,%u", encoded[0] & 255u, encoded[1] & 255u);
    else if (slot == 3) snprintf(piece, sizeof piece, "%u", encoded[0] & 255u);
    else if (slot == 4) snprintf(piece, sizeof piece, "%u,%u,%u", encoded[0] & 255u, encoded[1] >> 8, encoded[1] & 255u);
    else if (slot == 6) snprintf(piece, sizeof piece, "%u,%u,%u", encoded[0] & 255u, encoded[1] >> 8, encoded[1] & 127u);
    else return 0;
    return text_str(out, piece);
}

static int format_polygon(const unsigned *words, size_t count, fe2_text *out)
{
    static const char *names[] = {"end", "bezier", "line", "line_cont", "bezier_cont", "line_join", "circle", "adjacent_operator"};
    static const unsigned widths[] = {1, 3, 2, 1, 2, 1, 2, 1};
    size_t offset = 0;
    int any = 0;
    while (offset < count) {
        unsigned slot = (words[offset] >> 9) & 7u;
        unsigned width = widths[slot];
        size_t i;
        char encoded[80];
        if (offset + width > count) return -1;
        if (any && text_str(out, "|")) return -1;
        if (text_str(out, names[slot]) || text_str(out, ":")) return -1;
        if (polygon_semantic(slot, words + offset, out)) return -1;
        encoded[0] = 0;
        for (i = 0; i < width; ++i) {
            char word[16];
            snprintf(word, sizeof word, "%s0x%04x", i ? "," : "", words[offset + i]);
            if (strlen(encoded) + strlen(word) >= sizeof encoded) return -1;
            strcat(encoded, word);
        }
        if (text_str(out, "@") || text_str(out, encoded)) return -1;
        offset += width;
        any = 1;
        if (slot == 0) return offset == count ? 0 : -1;
    }
    return -1;
}

static int format_operation(unsigned token, const unsigned *words, size_t count, int truncated, fe2_text *out)
{
    unsigned slot = token & 31u;
    char line[8192];
    fe2_text extra;
    memset(&extra, 0, sizeof extra);
    if (slot == 24 && !truncated && count) {
        char size[64], kind[16];
        unsigned value;
        int inherit;
        unsigned selector = token >> 4;
        inherit = (selector & 0x10u) != 0;
        if ((selector & 0xf00u) == 0xe00u) {
            snprintf(kind, sizeof kind, "table");
            value = ((selector & 0xeu) << 3) | ((selector & 0xe0u) << 2);
        } else {
            snprintf(kind, sizeof kind, "rgb12");
            value = selector & 0xeeeu;
        }
        size_text(words[0], size, sizeof size);
        snprintf(line, sizeof line, "point_list token=0x%04x size=%s material=%s:0x%03x inherit_material=%s points=",
                 token, size, kind, value, inherit ? "true" : "false");
        if (text_str(out, line) || format_points(words + 1, count - 1, out)) { free(extra.data); return -1; }
        return 0;
    }
    if (slot == 31 && count >= 2) {
        size_t i;
        snprintf(line, sizeof line, "geometry_pipeline token=0x%04x words=0x%04x,0x%04x payload=", token, words[0], words[1]);
        if (text_str(out, line)) return -1;
        if (count == 2) { if (text_str(out, "-")) return -1; }
        else for (i = 2; i < count; ++i) {
            snprintf(line, sizeof line, "%02x%02x", words[i] & 255u, words[i] >> 8);
            if (text_str(out, line)) return -1;
        }
        if (truncated && text_str(out, " truncated=true")) return -1;
        return 0;
    }
    if (slot == 5 && !truncated && count > 1 && format_polygon(words + 1, count - 1, &extra) == 0) {
        unsigned header = words[0];
        snprintf(line, sizeof line,
                 "polygon_value_sequence token=0x%04x normal_index=0x%02x skip_bytes=0x%02x skip_if_z_clipped=%s header=0x%04x commands=%s",
                 token, header & 0x7fu, header >> 8, (header & 0x80u) ? "true" : "false", header, extra.data ? extra.data : "");
        free(extra.data);
        return text_str(out, line);
    }
    free(extra.data);
    snprintf(line, sizeof line, "%s token=0x%04x words=", operation_name(slot), token);
    if (text_str(out, line)) return -1;
    if (!count) return text_str(out, "-");
    {
        size_t i;
        for (i = 0; i < count; ++i) {
            snprintf(line, sizeof line, "%s0x%04x", i ? "," : "", words[i]);
            if (text_str(out, line)) return -1;
        }
    }
    if (truncated && text_str(out, " truncated=true")) return -1;
    return 0;
}

static int append_raw(fe2_text *out, const uint8_t *data, size_t from, size_t to)
{
    size_t start;
    for (start = from; start < to; start += 16) {
        size_t i, end = start + 16 < to ? start + 16 : to;
        if ((out->count && text_str(out, "\n")) || text_str(out, "raw")) return -1;
        for (i = start; i < end; ++i) {
            char byte[8];
            snprintf(byte, sizeof byte, " 0x%02x", data[i]);
            if (text_str(out, byte)) return -1;
        }
    }
    return 0;
}

static int finish_program(const uint8_t *data, size_t count, fe2_text *text, char **out)
{
    fe2_buf compiled;
    memset(&compiled, 0, sizeof compiled);
    if (text->count && text->data[text->count - 1] != '\n' && text_str(text, "\n")) return -1;
    if (fe2_compile_program(text->data ? text->data : "", NULL, NULL, &compiled) ||
        compiled.count != count || memcmp(compiled.data, data, count) != 0) {
        fe2_buf_free(&compiled);
        return -1;
    }
    fe2_buf_free(&compiled);
    *out = text->data ? text->data : calloc(1, 1);
    text->data = NULL;
    return *out ? 0 : -1;
}

static int decode_linear(const uint8_t *data, size_t count, char **out)
{
    fe2_text text;
    size_t offset = 0;
    memset(&text, 0, sizeof text);
    while (offset + 2 <= count) {
        unsigned token = read_u16(data + offset);
        unsigned words[4096];
        size_t available = (count - offset - 2) / 2, n = available > 4096 ? 4096 : available, i;
        int operands = 0;
        for (i = 0; i < n; ++i) words[i] = read_u16(data + offset + 2 + 2 * i);
        if (fe2_operand_count(token, words, n, &operands)) break;
        if (offset + 2 + (size_t)operands * 2 > count) {
            size_t have = (count - offset - 2) / 2;
            unsigned slot = token & 31u;
            if ((slot == 31 && have >= 2) || (slot != 31 && have >= 1)) {
                if ((text.count && text_str(&text, "\n")) || format_operation(token, words, have, 1, &text)) goto fail;
                offset += 2 + have * 2;
            }
            break;
        }
        if ((text.count && text_str(&text, "\n")) || format_operation(token, words, (size_t)operands, 0, &text)) goto fail;
        offset += 2 + (size_t)operands * 2;
        if ((token & 31u) == 0) break;
    }
    if (append_raw(&text, data, offset, count) || finish_program(data, count, &text, out)) goto fail;
    return 0;
fail:
    free(text.data);
    return -1;
}

int fe2_decode_program(const uint8_t *data, size_t count, const unsigned *entries, size_t entry_count, char **text)
{
    size_t *pending = NULL, pending_n = 0, pending_cap = 0;
    struct item { size_t start, end; char *text; int complete; } *items = NULL;
    size_t item_n = 0, item_cap = 0;
    size_t e, i;
    int status = -1;
    fe2_text rendered;
    if (!entries) entry_count = 0;
    for (e = 0; e < entry_count; ++e) if (entries[e] >= count) goto linear;
    pending_cap = entry_count + 1;
    pending = (size_t *)malloc(pending_cap * sizeof *pending);
    if (!pending) return -1;
    pending[pending_n++] = 0;
    for (e = 0; e < entry_count; ++e) pending[pending_n++] = entries[e];
    while (pending_n) {
        size_t offset = pending[--pending_n];
        while (offset + 2 <= count) {
            unsigned token, words[4096], slot;
            size_t available, n, word_i;
            int operands = 0, seen = 0;
            for (i = 0; i < item_n; ++i) if (items[i].start == offset) seen = 1;
            if (seen) break;
            token = read_u16(data + offset);
            available = (count - offset - 2) / 2;
            n = available > 4096 ? 4096 : available;
            for (word_i = 0; word_i < n; ++word_i) words[word_i] = read_u16(data + offset + 2 + 2 * word_i);
            if (fe2_operand_count(token, words, n, &operands)) break;
            if (offset + 2 + (size_t)operands * 2 > count) break;
            {
                size_t end = offset + 2 + (size_t)operands * 2;
                fe2_text line;
                for (i = 0; i < item_n; ++i)
                    if (offset < items[i].end && items[i].start < end) goto linear;
                memset(&line, 0, sizeof line);
                if (format_operation(token, words, (size_t)operands, 0, &line)) { free(line.data); goto linear; }
                if (item_n == item_cap) {
                    size_t cap = item_cap ? item_cap * 2 : 16;
                    struct item *grown = realloc(items, cap * sizeof *grown);
                    if (!grown) { free(line.data); goto linear; }
                    items = grown;
                    item_cap = cap;
                }
                items[item_n].start = offset;
                items[item_n].end = end;
                items[item_n].text = line.data;
                items[item_n].complete = 1;
                ++item_n;
                slot = token & 31u;
                if (slot == 11 || slot == 12 || slot == 19 || slot == 20 || slot == 23) {
                    unsigned skip = (token >> 4) & 0xfffeu;
                    if (skip && end + skip < count) {
                        if (pending_n == pending_cap) {
                            size_t cap = pending_cap ? pending_cap * 2 : 8;
                            size_t *grown = realloc(pending, cap * sizeof *grown);
                            if (!grown) goto linear;
                            pending = grown;
                            pending_cap = cap;
                        }
                        pending[pending_n++] = end + skip;
                    }
                } else if (slot == 5 && operands && (words[0] >> 8)) {
                    size_t target = offset + 4 + (words[0] >> 8);
                    if (target < count) {
                        if (pending_n == pending_cap) {
                            size_t cap = pending_cap ? pending_cap * 2 : 8;
                            size_t *grown = realloc(pending, cap * sizeof *grown);
                            if (!grown) goto linear;
                            pending = grown;
                            pending_cap = cap;
                        }
                        pending[pending_n++] = target;
                    }
                }
                offset = end;
                if (slot == 0) break;
            }
        }
    }
    for (i = 1; i < item_n; ++i) {
        size_t j = i;
        while (j && items[j - 1].start > items[j].start) {
            struct item swap = items[j - 1];
            items[j - 1] = items[j];
            items[j] = swap;
            --j;
        }
    }
    memset(&rendered, 0, sizeof rendered);
    {
        size_t cursor = 0;
        for (i = 0; i < item_n; ++i) {
            char comment[32];
            if (append_raw(&rendered, data, cursor, items[i].start)) goto linear;
            if ((rendered.count && text_str(&rendered, "\n")) || text_str(&rendered, items[i].text)) goto linear;
            snprintf(comment, sizeof comment, " # program +0x%04x", (unsigned)items[i].start);
            if (text_str(&rendered, comment)) goto linear;
            cursor = items[i].end;
        }
        if (append_raw(&rendered, data, cursor, count)) goto linear;
    }
    if (finish_program(data, count, &rendered, text)) { free(rendered.data); goto linear; }
    status = 0;
    goto done;
linear:
    for (i = 0; i < item_n; ++i) free(items[i].text);
    free(items);
    free(pending);
    items = NULL;
    pending = NULL;
    return decode_linear(data, count, text);
done:
    for (i = 0; i < item_n; ++i) free(items[i].text);
    free(items);
    free(pending);
    return status;
}

static int signed_byte(unsigned value)
{
    return value < 128 ? (int)value : (int)value - 256;
}

int fe2_decode_geometry(const uint8_t *data, size_t count, char **text)
{
    fe2_text out;
    size_t offset = 0;
    fe2_buf check;
    memset(&out, 0, sizeof out);
    memset(&check, 0, sizeof check);
    if (count >= 8) {
        unsigned flags = read_u16(data);
        size_t extra = (flags & 0x1000u) ? 58 : ((flags & 0x4000u) ? 0 : 8);
        if (count >= 8 + extra) {
            size_t i;
            if (text_str(&out, "primary_words")) return -1;
            for (i = 0; i < 4; ++i) {
                char word[16];
                snprintf(word, sizeof word, " 0x%04x", read_u16(data + 2 * i));
                if (text_str(&out, word)) goto fail;
            }
            if (flags & 0x1000u) {
                if (text_str(&out, "\nselection_words")) goto fail;
                for (i = 0; i < 29; ++i) {
                    char word[16];
                    snprintf(word, sizeof word, " 0x%04x", read_u16(data + 8 + 2 * i));
                    if (text_str(&out, word)) goto fail;
                }
            } else if (!(flags & 0x4000u)) {
                if (text_str(&out, "\nsecondary_words")) goto fail;
                for (i = 0; i < 4; ++i) {
                    char word[16];
                    snprintf(word, sizeof word, " 0x%04x", read_u16(data + 8 + 2 * i));
                    if (text_str(&out, word)) goto fail;
                }
            }
            offset = 8 + extra;
            if (offset + 2 <= count) {
                unsigned base = read_u16(data + offset);
                fe2_text layers;
                size_t layer = offset + 2;
                memset(&layers, 0, sizeof layers);
                {
                    char line[32];
                    snprintf(line, sizeof line, "layer_base 0x%04x", base);
                    text_str(&layers, line);
                }
                if (base == 0) {
                    if (text_str(&out, "\n") || text_str(&out, layers.data)) { free(layers.data); goto fail; }
                    offset = layer;
                }
                while (base && layer + 2 <= count) {
                    unsigned scale = read_u16(data + layer);
                    layer += 2;
                    if (scale == 0) {
                        if (text_str(&layers, "\nend_layers") || text_str(&out, "\n") || text_str(&out, layers.data)) {
                            free(layers.data); goto fail;
                        }
                        offset = layer;
                        break;
                    }
                    if (layer + 2 > count) break;
                    {
                        char line[40];
                        snprintf(line, sizeof line, "\nlayer 0x%04x 0x%04x", scale, read_u16(data + layer));
                        layer += 2;
                        if (text_str(&layers, line)) { free(layers.data); goto fail; }
                    }
                }
                free(layers.data);
            }
        }
    }
    {
        size_t cursor = offset;
        while (cursor < count) {
            unsigned tag = data[cursor];
            size_t begin = cursor++;
            if (tag == 0) {
                if (text_str(&out, out.count ? "\nend_records" : "end_records")) goto fail;
                offset = cursor;
                break;
            }
            if (tag >= 128) {
                char line[80];
                if (cursor + 4 > count) { offset = begin; break; }
                snprintf(line, sizeof line, "%svector_phase tag=0x%02x xyz=%d,%d,%d phase=%u",
                         out.count ? "\n" : "", tag, signed_byte(data[cursor]), signed_byte(data[cursor + 1]),
                         signed_byte(data[cursor + 2]), data[cursor + 3]);
                cursor += 4;
                if (text_str(&out, line)) goto fail;
                continue;
            }
            if (cursor >= count) { offset = begin; break; }
            {
                int selector = signed_byte(data[cursor++]);
                char group[1024];
                size_t used = 0;
                int started = 0;
                snprintf(group, sizeof group, "%sbegin_curve tag=%u selector=%d", out.count ? "\n" : "", tag, selector);
                used = strlen(group);
                while (1) {
                    char line[80];
                    if (cursor + 4 > count) { offset = begin; goto records_done; }
                    snprintf(line, sizeof line, "\n%s xyz=%d,%d,%d", data[cursor + 3] ? "curve_start" : "isolated_point",
                             signed_byte(data[cursor]), signed_byte(data[cursor + 1]), signed_byte(data[cursor + 2]));
                    cursor += 3;
                    if (data[cursor] != 0) {
                        if (used + strlen(line) >= sizeof group) goto fail;
                        strcat(group, line);
                        ++cursor;
                        started = 1;
                        break;
                    }
                    if (cursor + 2 > count) { offset = begin; goto records_done; }
                    snprintf(line, sizeof line, "\nisolated_point xyz=%d,%d,%d parameter=%u",
                             signed_byte(data[cursor - 3]), signed_byte(data[cursor - 2]), signed_byte(data[cursor - 1]), data[cursor + 1]);
                    cursor += 2;
                    if (used + strlen(line) >= sizeof group) goto fail;
                    strcat(group, line);
                    used = strlen(group);
                }
                while (started && cursor < count && data[cursor] != 0) {
                    char line[64];
                    if (cursor + 3 > count) { offset = begin; goto records_done; }
                    snprintf(line, sizeof line, "\ncurve_point xyz=%d,%d,%d", signed_byte(data[cursor]),
                             signed_byte(data[cursor + 1]), signed_byte(data[cursor + 2]));
                    cursor += 3;
                    if (strlen(group) + strlen(line) >= sizeof group) goto fail;
                    strcat(group, line);
                }
                if (cursor >= count) { offset = begin; break; }
                ++cursor;
                if (strlen(group) + 12 >= sizeof group) goto fail;
                strcat(group, "\nend_curve");
                if (text_str(&out, group)) goto fail;
                offset = cursor;
            }
        }
    }
records_done:
    if (out.count && strcmp(out.data + out.count - 11, "end_records") == 0 && (offset % 2) && offset < count) {
        char line[32];
        snprintf(line, sizeof line, "\npadding 0x%02x", data[offset]);
        if (text_str(&out, line)) goto fail;
        ++offset;
    }
    if (append_raw(&out, data, offset, count)) goto fail;
    if (out.count && out.data[out.count - 1] != '\n' && text_str(&out, "\n")) goto fail;
    if (fe2_compile_geometry(out.data ? out.data : "", &check) || check.count != count ||
        memcmp(check.data, data, count) != 0) {
        fe2_buf_free(&check);
        goto fail;
    }
    fe2_buf_free(&check);
    *text = out.data ? out.data : calloc(1, 1);
    return *text ? 0 : -1;
fail:
    free(out.data);
    return -1;
}

int fe2_decode_contact(const uint8_t *data, size_t count, size_t start, char **text, size_t *next)
{
    fe2_text out;
    size_t position = start;
    fe2_buf check;
    memset(&out, 0, sizeof out);
    memset(&check, 0, sizeof check);
    if (text_str(&out, "# Recovered model contact program; tag/result low bits retain original values.\n")) return -1;
    while (position < count) {
        unsigned tag = data[position++];
        char line[256];
        if (!tag) {
            if (text_str(&out, "end\n")) goto fail;
            break;
        }
        if (position >= count) goto fail;
        {
            const char *name = (tag & 0xc0u) == 0 ? "planes_all_le" : (tag & 0xc0u) == 0x40u ? "planes_all_gt" :
                (tag & 0xc0u) == 0x80u ? "child" : "result_planes_all_le";
            unsigned result = data[position++];
            snprintf(line, sizeof line, "%s tag=0x%02x result=0x%02x", name, tag, result);
            if (text_str(&out, line)) goto fail;
            if (tag & 0x20u) {
                if (position + 1 >= count) goto fail;
                snprintf(line, sizeof line, " field=%u shift=0x%02x", data[position], data[position + 1]);
                position += 2;
                if (text_str(&out, line)) goto fail;
            }
            if ((tag & 0xc0u) == 0x80u) {
                if (position + 3 >= count) goto fail;
                snprintf(line, sizeof line, " model=%u transform=0x%02x vertex=%u",
                         data[position] * 256u + data[position + 1], data[position + 2], data[position + 3]);
                position += 4;
                if (text_str(&out, line)) goto fail;
            } else {
                int any = 0;
                if (text_str(&out, " refs=")) goto fail;
                while (position < count && data[position]) {
                    snprintf(line, sizeof line, "%s%u", any ? "," : "", data[position]);
                    if (text_str(&out, line)) goto fail;
                    any = 1;
                    ++position;
                }
                if (position >= count) goto fail;
                ++position;
                if (!any && text_str(&out, "-")) goto fail;
            }
            if (text_str(&out, "\n")) goto fail;
        }
    }
    if (fe2_compile_contact(out.data, &check) || check.count != position - start ||
        memcmp(check.data, data + start, check.count) != 0) {
        fe2_buf_free(&check); goto fail;
    }
    fe2_buf_free(&check);
    *text = out.data;
    *next = position;
    return 0;
fail:
    free(out.data);
    return -1;
}

int fe2_decode_offsets(const uint8_t *data, size_t count, char **text)
{
    fe2_text out;
    fe2_buf check;
    size_t offset;
    memset(&out, 0, sizeof out);
    memset(&check, 0, sizeof check);
    if (!count || count % 4) return -1;
    if (text_str(&out, "# Physical storage order; contact traversal visits these pairs backward.\n"
                       "# Zero length stops without consuming the preceding word.\n")) return -1;
    for (offset = 0; offset < count; offset += 4) {
        unsigned selector = read_u16(data + offset);
        int length = read_s16(data + offset + 2);
        char line[96];
        if (length == 0) snprintf(line, sizeof line, "stop unused_word=0x%04x\n", selector);
        else snprintf(line, sizeof line, "basis_offset selector=0x%04x basis_word_offset=%u negate=%s length=%d\n",
                      selector, selector & 6u, (selector & 1u) ? "true" : "false", length);
        if (text_str(&out, line)) goto fail;
    }
    if (fe2_compile_offsets(out.data, &check) || check.count != count || memcmp(check.data, data, count) != 0) {
        fe2_buf_free(&check); goto fail;
    }
    fe2_buf_free(&check);
    *text = out.data;
    return 0;
fail:
    free(out.data);
    return -1;
}
