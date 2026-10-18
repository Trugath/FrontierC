/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Compile gameplay table images while packing frontier.dat.
 * The bytes match the Python table compilers for the same data tree. */

#include "asset_tables.h"

#include "asset_files.h"
#include "asset_ini.h"
#include "asset_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    MARKET_BASE = 0xa2,
    MARKET_END = 0x6eb,
    MARKET_BYTES = MARKET_END - MARKET_BASE,
    CENTRE_X = 0x1718,
    CENTRE_Y = 0x1524
};

typedef struct fe2_csv_row {
    char **cell;
    size_t n;
} fe2_csv_row;

typedef struct fe2_csv {
    fe2_csv_row *rows;
    size_t n;
} fe2_csv;

typedef struct fe2_span {
    unsigned begin, end;
} fe2_span;

typedef struct fe2_text_cat {
    const char *name;
    unsigned base;
    char **ident;
    size_t count;
} fe2_text_cat;

typedef struct fe2_names {
    char **chart;
    size_t chart_n;
    char syllables[32][16];
    char **model_name;
    unsigned *model_slot;
    size_t model_n;
    fe2_text_cat text[5];
} fe2_names;

static const unsigned attachment_banks[] = {0x00, 2, 0x0e, 4, 0x2c, 6};
static const char *attachment_bank_names[] = {"shape_1", "shape_3", "other"};
static const unsigned command_banks[] = {0x00, 4, 0x1e, 4, 0x3c, 4, 0x5a, 4};
static const char *command_bank_names[] = {"shape_1", "shape_3", "other", "shape_0"};
static const unsigned control_banks[] = {0x00, 4, 0x1e, 4};
static const char *control_bank_names[] = {"free", "fixed"};

static const char *offer_names[] = {
    "independent_goods", "independent_equipment", "imperial_goods", "imperial_equipment",
    "federation_goods", "federation_equipment", "kind_6", "kind_7", "kind_8",
    "station_ships", "kind_10", "kind_11", "from_detail"
};
static const unsigned offer_at[] = {0x98, 0xa6, 0x7e, 0x8c, 0x5a, 0x68, 0xbe, 0xce, 0xf2, 0x100, 0x104, 0x108, 0x10e};
static const unsigned offer_count[] = {7, 12, 7, 6, 7, 11, 8, 18, 7, 2, 2, 3, 11};

static const char *population_labels[] = {
    "None registered", "Less than 1,000", "1,000-10,000", "10,000-100,000",
    "100,000-1 Million", "1-10 Million", "10-100 Million", "100 Million-1 Billion",
    "1-10 Billion", "More than 10 Billion"
};

static const char *handlers[] = {
    "generate_illegal_commodity_flag_record", "generate_market_flag_record_2",
    "generate_market_flag_record_8", "generate_record_from_system_history",
    "generate_record_from_random_seed", "generate_record_from_candidate_system",
    "generate_market_flag_record_4", "generate_variant_text_record",
    "generate_remote_system_record", "generate_random_text_record",
    "generate_request_variant_8", "generate_request_variant_4",
    "generate_request_variant_4_with_threshold", "generate_assassinate_request",
    "generate_photograph_request", "generate_destroy_request"
};
static const unsigned handler_at[] = {
    0x12dc, 0x132b, 0x139f, 0x10b2, 0x118e, 0x11ac, 0x135e, 0x14e4,
    0x13cd, 0x14d0, 0xce9, 0xcf4, 0xd0d, 0xbb0, 0xc19, 0xc81
};

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

static int parse_dec(const char *text, long long *out)
{
    const char *cursor = text;
    char *end;
    if (!text || !text[0]) return -1;
    if (*cursor == '+' || *cursor == '-') ++cursor;
    if (!*cursor) return -1;
    for (; *cursor; ++cursor) if (*cursor < '0' || *cursor > '9') return -1;
    *out = strtoll(text, &end, 10);
    if (end == text || *end) return -1;
    return 0;
}

static int under(const char *root, const char *rel, char *out, size_t cap)
{
    return fe2_path_join(out, cap, root, rel);
}

static void trim(char *text)
{
    char *start = text;
    size_t n;
    while (*start == ' ' || *start == '\t') ++start;
    if (start != text) memmove(text, start, strlen(start) + 1);
    n = strlen(text);
    while (n && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\r')) text[--n] = 0;
}

static void csv_free(fe2_csv *csv)
{
    size_t r, c;
    if (!csv) return;
    for (r = 0; r < csv->n; ++r) {
        for (c = 0; c < csv->rows[r].n; ++c) free(csv->rows[r].cell[c]);
        free(csv->rows[r].cell);
    }
    free(csv->rows);
    memset(csv, 0, sizeof *csv);
}

static int csv_field(const char **cursor, char **out, char delim)
{
    const char *p = *cursor;
    fe2_buf buf;
    memset(&buf, 0, sizeof buf);
    if (*p == '"') {
        ++p;
        for (;;) {
            if (!*p) { fe2_buf_free(&buf); return -1; }
            if (*p == '"') {
                if (p[1] == '"') {
                    if (fe2_buf_byte(&buf, '"')) { fe2_buf_free(&buf); return -1; }
                    p += 2;
                    continue;
                }
                ++p;
                break;
            }
            if (fe2_buf_byte(&buf, (uint8_t)*p)) { fe2_buf_free(&buf); return -1; }
            ++p;
        }
    } else {
        while (*p && *p != delim && *p != '\n' && *p != '\r') {
            if (fe2_buf_byte(&buf, (uint8_t)*p)) { fe2_buf_free(&buf); return -1; }
            ++p;
        }
    }
    if (fe2_buf_byte(&buf, 0)) { fe2_buf_free(&buf); return -1; }
    *cursor = p;
    *out = (char *)buf.data;
    return 0;
}

static int csv_push(fe2_csv_row *row, char *cell)
{
    char **grown = (char **)realloc(row->cell, (row->n + 1) * sizeof *grown);
    if (!grown) return -1;
    row->cell = grown;
    row->cell[row->n++] = cell;
    return 0;
}

static int csv_load_delim(const char *path, fe2_csv *out, char delim)
{
    char *text, *body;
    const char *cursor;
    memset(out, 0, sizeof *out);
    text = fe2_read_text(path, NULL);
    if (!text) return fe2_asset_fail("Could not read %s", path);
    body = text;
    if ((unsigned char)body[0] == 0xef && (unsigned char)body[1] == 0xbb && (unsigned char)body[2] == 0xbf)
        body += 3;
    cursor = body;
    while (*cursor) {
        fe2_csv_row row;
        int blank = 1;
        size_t i;
        memset(&row, 0, sizeof row);
        if (*cursor == '\n') { ++cursor; continue; }
        if (*cursor == '\r' && cursor[1] == '\n') { cursor += 2; continue; }
        for (;;) {
            char *field = NULL;
            if (csv_field(&cursor, &field, delim)) { free(field); goto bad_row; }
            trim(field);
            if (field[0]) blank = 0;
            if (csv_push(&row, field)) { free(field); goto bad_row; }
            if (*cursor == delim) { ++cursor; continue; }
            break;
        }
        if (*cursor == '\r') ++cursor;
        if (*cursor == '\n') ++cursor;
        if (blank) {
            for (i = 0; i < row.n; ++i) free(row.cell[i]);
            free(row.cell);
            continue;
        }
        {
            fe2_csv_row *grown = (fe2_csv_row *)realloc(out->rows, (out->n + 1) * sizeof *grown);
            if (!grown) goto bad_row;
            out->rows = grown;
            out->rows[out->n++] = row;
        }
        continue;
    bad_row:
        for (i = 0; i < row.n; ++i) free(row.cell[i]);
        free(row.cell);
        free(text);
        csv_free(out);
        return fe2_asset_fail("Could not read %s", path);
    }
    free(text);
    if (!out->n) return fe2_asset_fail("Could not read %s", path);
    return 0;
}

static int csv_load(const char *path, fe2_csv *out)
{
    return csv_load_delim(path, out, ',');
}

static const char *csv_cell(const fe2_csv *csv, size_t row, size_t col)
{
    if (row >= csv->n || col >= csv->rows[row].n) return NULL;
    return csv->rows[row].cell[col];
}

static int csv_header(const fe2_csv *csv, const char *const *names, size_t n)
{
    size_t i;
    if (!csv->n || csv->rows[0].n != n) return -1;
    for (i = 0; i < n; ++i) if (strcmp(csv->rows[0].cell[i], names[i]) != 0) return -1;
    return 0;
}

static void free_lines(char **lines, size_t count)
{
    size_t i;
    if (!lines) return;
    for (i = 0; i < count; ++i) free(lines[i]);
    free(lines);
}

static int load_lines(const char *path, char ***out, size_t *count, int skip_empty)
{
    char *text = fe2_read_text(path, NULL);
    char *cursor;
    char **lines = NULL;
    size_t n = 0;
    if (!text) return fe2_asset_fail("Could not read %s", path);
    cursor = text;
    if ((unsigned char)cursor[0] == 0xef && (unsigned char)cursor[1] == 0xbb && (unsigned char)cursor[2] == 0xbf)
        cursor += 3;
    while (*cursor) {
        char *eol = strchr(cursor, '\n');
        size_t length;
        char *copy, **grown;
        if (eol) *eol = 0;
        length = strlen(cursor);
        if (length && cursor[length - 1] == '\r') cursor[--length] = 0;
        if (!(skip_empty && !length)) {
            copy = (char *)malloc(length + 1);
            grown = copy ? (char **)realloc(lines, (n + 1) * sizeof *lines) : NULL;
            if (!grown) { free(copy); free_lines(lines, n); free(text); return -1; }
            lines = grown;
            memcpy(copy, cursor, length + 1);
            lines[n++] = copy;
        }
        if (!eol) break;
        cursor = eol + 1;
        if (!*cursor) break;
    }
    free(text);
    *out = lines;
    *count = n;
    return 0;
}

static void names_free(fe2_names *names)
{
    size_t i;
    if (!names) return;
    free_lines(names->chart, names->chart_n);
    free_lines(names->model_name, names->model_n);
    free(names->model_slot);
    for (i = 0; i < 5; ++i) free_lines(names->text[i].ident, names->text[i].count);
    memset(names, 0, sizeof *names);
}

static int load_models(fe2_names *names, const char *root)
{
    char path[4096];
    char **lines = NULL;
    size_t count = 0, i;
    if (under(root, "models/frontier/model_directory.tsv", path, sizeof path) ||
        load_lines(path, &lines, &count, 1)) return -1;
    for (i = 0; i < count; ++i) {
        char *tab = strchr(lines[i], '\t');
        long long slot;
        size_t n;
        char **grown_names;
        unsigned *grown_slots;
        if (!tab || tab == lines[i] || !tab[1]) { free_lines(lines, count); return fe2_asset_fail("Unknown model %s", lines[i]); }
        *tab = 0;
        if (!strcmp(tab + 1, "-")) continue;
        if (parse_i64(lines[i], &slot) || slot < 0 || slot > 65535) { free_lines(lines, count); return -1; }
        for (n = 0; n < names->model_n; ++n) if (!strcmp(names->model_name[n], tab + 1)) {
            free_lines(lines, count);
            return fe2_asset_fail("Duplicate model name %s", tab + 1);
        }
        grown_names = (char **)realloc(names->model_name, (names->model_n + 1) * sizeof *grown_names);
        if (!grown_names) { free_lines(lines, count); return -1; }
        names->model_name = grown_names;
        grown_slots = (unsigned *)realloc(names->model_slot, (names->model_n + 1) * sizeof *grown_slots);
        if (!grown_slots) { free_lines(lines, count); return -1; }
        names->model_slot = grown_slots;
        names->model_name[names->model_n] = (char *)malloc(strlen(tab + 1) + 1);
        if (!names->model_name[names->model_n]) { free_lines(lines, count); return -1; }
        memcpy(names->model_name[names->model_n], tab + 1, strlen(tab + 1) + 1);
        names->model_slot[names->model_n] = (unsigned)slot;
        ++names->model_n;
    }
    free_lines(lines, count);
    return 0;
}

static int load_texts(fe2_names *names, const char *root)
{
    static const char *cats[] = {"systems", "chart", "flight", "interaction", "status"};
    static const unsigned bases[] = {0x8000, 0x8400, 0x8600, 0x9800, 0x9c00};
    size_t c, i;
    for (c = 0; c < 5; ++c) {
        char rel[128], path[4096];
        names->text[c].name = cats[c];
        names->text[c].base = bases[c];
        if (snprintf(rel, sizeof rel, "text/%s/identifiers.txt", cats[c]) >= (int)sizeof rel ||
            under(root, rel, path, sizeof path) ||
            load_lines(path, &names->text[c].ident, &names->text[c].count, 1)) return -1;
        for (i = 0; i < names->text[c].count; ++i) {
            size_t j;
            for (j = 0; j < i; ++j) if (!strcmp(names->text[c].ident[i], names->text[c].ident[j]))
                return fe2_asset_fail("Duplicate text identifiers in %s", cats[c]);
        }
    }
    return 0;
}

static int resolve_text(const fe2_names *names, const char *reference, unsigned *out)
{
    const char *colon = strchr(reference, ':');
    size_t c, i;
    if (!colon || colon == reference) return -1;
    for (c = 0; c < 5; ++c) {
        size_t n = strlen(names->text[c].name);
        if ((size_t)(colon - reference) != n || strncmp(reference, names->text[c].name, n)) continue;
        for (i = 0; i < names->text[c].count; ++i) {
            if (!strcmp(colon + 1, names->text[c].ident[i])) {
                unsigned selector = names->text[c].base + (unsigned)i;
                if (selector > 0xffffu) return -1;
                *out = selector;
                return 0;
            }
        }
        return -1;
    }
    return -1;
}

static int parse_model(const fe2_names *names, const char *token, unsigned *out)
{
    size_t i;
    long long value;
    for (i = 0; i < names->model_n; ++i) if (!strcmp(names->model_name[i], token)) {
        *out = names->model_slot[i];
        return 0;
    }
    if (parse_i64(token, &value) || value < 0 || value > 65535) return fe2_asset_fail("Unknown model %s", token);
    for (i = 0; i < names->model_n; ++i) if (names->model_slot[i] == (unsigned)value)
        return fe2_asset_fail("%s is %s; write that name", token, names->model_name[i]);
    *out = (unsigned)value;
    return 0;
}

static int decode_text(const char *value, fe2_buf *out)
{
    const char *cursor = value;
    while (*cursor) {
        if (*cursor == '\\') {
            unsigned byte;
            if (cursor[1] == '\\') { if (fe2_buf_byte(out, 92)) return -1; cursor += 2; continue; }
            if (cursor[1] != 'x' || !cursor[2] || !cursor[3]) return fe2_asset_fail("Expected \\\\ or \\xHH escape");
            if (!((cursor[2] >= '0' && cursor[2] <= '9') || (cursor[2] >= 'A' && cursor[2] <= 'F') || (cursor[2] >= 'a' && cursor[2] <= 'f')) ||
                !((cursor[3] >= '0' && cursor[3] <= '9') || (cursor[3] >= 'A' && cursor[3] <= 'F') || (cursor[3] >= 'a' && cursor[3] <= 'f')))
                return fe2_asset_fail("Expected \\\\ or \\xHH escape");
            if (sscanf(cursor + 2, "%2x", &byte) != 1 || fe2_buf_byte(out, (uint8_t)byte)) return -1;
            cursor += 4;
            continue;
        }
        if ((unsigned char)*cursor < 32 || (unsigned char)*cursor >= 127)
            return fe2_asset_fail("Escape non-ASCII or control bytes as \\xHH");
        if (fe2_buf_byte(out, (uint8_t)*cursor)) return -1;
        ++cursor;
    }
    return 0;
}

static int ident_ok(const char *name)
{
    const char *cursor;
    if (!name[0]) return 0;
    if (!((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_')) return 0;
    for (cursor = name + 1; *cursor; ++cursor)
        if (!((*cursor >= 'A' && *cursor <= 'Z') || (*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= '0' && *cursor <= '9') || *cursor == '_')) return 0;
    return 1;
}

static const char *strip_text(const char *ident)
{
    if (!strncmp(ident, "text_", 5)) return ident + 5;
    return ident;
}

static unsigned rol16(unsigned value, unsigned count)
{
    count &= 15u;
    value &= 0xffffu;
    if (!count) return value;
    return ((value << count) | (value >> (16u - count))) & 0xffffu;
}

static int chart_names(fe2_names *names, const char *root)
{
    char path[4096];
    size_t i;
    if (names->chart) return 0;
    if (under(root, "text/chart/identifiers.txt", path, sizeof path) ||
        load_lines(path, &names->chart, &names->chart_n, 0)) return -1;
    if (names->chart_n != 261) return fe2_asset_fail("chart text catalogue changed");
    for (i = 0; i < names->chart_n; ++i) {
        size_t j;
        for (j = 0; j < i; ++j) if (!strcmp(names->chart[i], names->chart[j]))
            return fe2_asset_fail("chart text catalogue changed");
    }
    return 0;
}

static int load_syllables(fe2_names *names, const char *root)
{
    char path[4096];
    char **lines = NULL;
    size_t count = 0, i;
    if (names->syllables[0][0]) return 0;
    if (under(root, "maps/system_name_syllables.txt", path, sizeof path) ||
        load_lines(path, &lines, &count, 0) || count != 32) {
        free_lines(lines, count);
        return fe2_asset_fail("chart syllable shape changed");
    }
    for (i = 0; i < 32; ++i) {
        if (strlen(lines[i]) >= sizeof names->syllables[i]) { free_lines(lines, count); return -1; }
        memcpy(names->syllables[i], lines[i], strlen(lines[i]) + 1);
    }
    free_lines(lines, count);
    return 0;
}

static int map_name(fe2_names *names, const char *root, int sector_x, int sector_y, int number,
                    const int *coord_x, const int *coord_y, const unsigned *bounds, char *out, size_t cap)
{
    long long map_x, map_y;
    int index;
    if (chart_names(names, root)) return -1;
    map_x = (long long)sector_x + CENTRE_X;
    map_y = (long long)sector_y + CENTRE_Y;
    if (map_x < 0 || map_x >= 8192 || map_y < 0 || map_y >= 8192 || number < 0 || number >= 64)
        return fe2_asset_fail("system coordinates are outside the map");
    for (index = 46; index >= 0; --index) {
        unsigned text;
        if (coord_x[index] != (int)map_x || coord_y[index] != (int)map_y) continue;
        text = bounds[index] / 2u + (unsigned)number;
        if (text >= names->chart_n) return fe2_asset_fail("system number is outside the chart text catalogue");
        if (strncmp(names->chart[text], "text_", 5))
            return fe2_asset_fail("catalog system is not a chart text identifier");
        if (strlen(names->chart[text] + 5) >= cap) return -1;
        memcpy(out, names->chart[text] + 5, strlen(names->chart[text] + 5) + 1);
        return 0;
    }
    if (load_syllables(names, root)) return -1;
    {
        unsigned a = ((unsigned)map_x + (unsigned)number) & 0xffffu;
        unsigned b = ((unsigned)map_y + a) & 0xffffu;
        unsigned mix;
        char shown[48];
        size_t used = 0;
        int part;
        a = (rol16(a, 3) + b) & 0xffffu;
        b = (rol16(b, 5) + a) & 0xffffu;
        mix = (rol16(a, (unsigned)number & 31u) + rol16(b, 4)) & 0xffffu;
        shown[0] = 0;
        for (part = 0; part < 3; ++part) {
            const char *syllable = names->syllables[(mix & 124u) >> 2];
            size_t n = strlen(syllable);
            if (used + n >= sizeof shown) return -1;
            memcpy(shown + used, syllable, n + 1);
            used += n;
            mix = rol16(mix, 11);
        }
        if (!used || used >= cap) return -1;
        out[0] = shown[0];
        if ((unsigned char)out[0] >= 32) {
            unsigned char folded = (unsigned char)((unsigned char)out[0] - 32u);
            if (folded >= 'A' && folded <= 'Z') folded = (unsigned char)(folded - 'A' + 'a');
            out[0] = (char)folded;
        }
        memcpy(out + 1, shown + 1, used);
        return 0;
    }
}

static int allegiance_label(fe2_names *names, const char *root, unsigned allegiance, char *out, size_t cap)
{
    unsigned doubled, slot;
    const char *ident;
    if (allegiance > 2 || chart_names(names, root)) return -1;
    doubled = ((0x84b7u + allegiance) << 1) & 0xffffu;
    slot = (doubled & 0x3feu) / 2u;
    ident = slot == 0 ? names->chart[names->chart_n - 1] : names->chart[slot - 1];
    if (strncmp(ident, "text_", 5)) return fe2_asset_fail("chart allegiance label is not a text identifier");
    if (strlen(ident + 5) >= cap) return -1;
    memcpy(out, ident + 5, strlen(ident + 5) + 1);
    return 0;
}

static int blank_fields(const fe2_csv *csv, size_t row, const size_t *cols, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        const char *cell = csv_cell(csv, row, cols[i]);
        if (cell && cell[0]) return -1;
    }
    return 0;
}

static int catalog_layout(const char *root, int *coord_x, int *coord_y, unsigned *bounds)
{
    static const char *header[] = {
        "role", "sector_x", "sector_y", "draw_kind", "system", "style", "depth", "place_x", "place_y", "identifier"
    };
    char path[4096];
    fe2_csv csv;
    fe2_names *probe;
    int sectors_x[47], sectors_y[47], open[47];
    unsigned draw[47];
    int counts[47];
    int nsectors = 0, ncounts = 0, systems = 0, have_tail = 0, have_draw = 0;
    int have_key = 0, key_x = 0, key_y = 0;
    unsigned key_draw = 0;
    size_t row;
    int status = -1;
    long long sum = 0;
    unsigned at = 0;
    memset(&csv, 0, sizeof csv);
    probe = (fe2_names *)calloc(1, sizeof *probe);
    if (!probe) return -1;
    if (under(root, "maps/catalog_systems.csv", path, sizeof path) || csv_load(path, &csv) ||
        csv_header(&csv, header, 10) || chart_names(probe, root)) goto done;
    for (row = 1; row < csv.n; ++row) {
        const char *role;
        long long sx, sy, kind;
        if (csv.rows[row].n != 10) goto done;
        role = csv.rows[row].cell[0];
        if (!strcmp(role, "system")) {
            int same;
            if (parse_dec(csv.rows[row].cell[1], &sx) || parse_dec(csv.rows[row].cell[2], &sy) ||
                parse_i64(csv.rows[row].cell[3], &kind) || sx < -65535 || sx > 65535 || sy < -65535 || sy > 65535 ||
                kind < 0 || kind > 65535) goto done;
            same = nsectors && sectors_x[nsectors - 1] == (int)sx && sectors_y[nsectors - 1] == (int)sy &&
                draw[nsectors - 1] == (unsigned)kind && open[nsectors - 1];
            if (!same) {
                if (nsectors == 47) goto done;
                sectors_x[nsectors] = (int)sx;
                sectors_y[nsectors] = (int)sy;
                draw[nsectors] = (unsigned)kind;
                open[nsectors] = 1;
                ++nsectors;
            }
            if (systems >= 117 || strcmp(csv.rows[row].cell[4], strip_text(probe->chart[systems]))) goto done;
            ++systems;
            if (!have_key || key_x != (int)sx || key_y != (int)sy || key_draw != (unsigned)kind) {
                if (ncounts == 47) goto done;
                counts[ncounts++] = 0;
                key_x = (int)sx;
                key_y = (int)sy;
                key_draw = (unsigned)kind;
                have_key = 1;
            }
            ++counts[ncounts - 1];
        } else if (!strcmp(role, "empty")) {
            size_t used[] = {4, 5, 6, 7, 8, 9};
            if (blank_fields(&csv, row, used, 6) || parse_dec(csv.rows[row].cell[1], &sx) ||
                parse_dec(csv.rows[row].cell[2], &sy) || parse_i64(csv.rows[row].cell[3], &kind) ||
                sx < -65535 || sx > 65535 || sy < -65535 || sy > 65535 || kind < 0 || kind > 65535 ||
                nsectors == 47 || ncounts == 47) goto done;
            if (nsectors) open[nsectors - 1] = 0;
            sectors_x[nsectors] = (int)sx;
            sectors_y[nsectors] = (int)sy;
            draw[nsectors] = (unsigned)kind;
            open[nsectors] = 0;
            ++nsectors;
            counts[ncounts++] = 0;
            have_key = 0;
        } else if (!strcmp(role, "coordinate_tail")) {
            size_t used[] = {3, 4, 5, 6, 7, 8, 9};
            long long x, y;
            if (have_tail || blank_fields(&csv, row, used, 7) || parse_dec(csv.rows[row].cell[1], &sx) ||
                parse_dec(csv.rows[row].cell[2], &sy)) goto done;
            x = sx + CENTRE_X;
            y = sy + CENTRE_Y;
            if (x < 0 || x > 65535 || y < 0 || y > 65535) goto done;
            have_tail = 1;
        } else if (!strcmp(role, "draw_kind_tail")) {
            size_t used[] = {1, 2, 4, 5, 6, 7, 8, 9};
            if (have_draw || blank_fields(&csv, row, used, 8) || parse_i64(csv.rows[row].cell[3], &kind) ||
                kind < 0 || kind > 65535) goto done;
            have_draw = 1;
        } else goto done;
    }
    if (nsectors != 47 || systems != 117 || ncounts != 47 || !have_tail || !have_draw) goto done;
    bounds[0] = 0;
    for (systems = 0; systems < 47; ++systems) {
        long long x = (long long)sectors_x[systems] + CENTRE_X;
        long long y = (long long)sectors_y[systems] + CENTRE_Y;
        if (x < 0 || x > 65535 || y < 0 || y > 65535) goto done;
        coord_x[systems] = (int)x;
        coord_y[systems] = (int)y;
        sum += counts[systems];
        at += (unsigned)counts[systems] * 2u;
        bounds[systems + 1] = at;
    }
    if (sum != 117) goto done;
    bounds[48] = at;
    bounds[49] = at;
    status = 0;
done:
    names_free(probe);
    free(probe);
    csv_free(&csv);
    if (status) fe2_asset_fail("catalogue sector layout changed");
    return status;
}

static int compile_market_text(const char *root, fe2_buf *out, char names[31][64])
{
    fe2_ini ini;
    fe2_ini_section *section;
    char path[4096], base[4096], entries[4096];
    const char *dir = NULL, *end_text = NULL, *count_text = NULL, *file = NULL;
    char *text = NULL, *cursor;
    fe2_buf strings;
    unsigned offsets[31];
    size_t count = 0, i, at, used = 0;
    long long expected, directory, end_offset;
    int status = -1;
    memset(&ini, 0, sizeof ini);
    memset(&strings, 0, sizeof strings);
    if (under(root, "text/market/catalog.ini", path, sizeof path) || fe2_ini_load(&ini, path)) {
        fe2_asset_fail("Could not read %s", path);
        goto done;
    }
    section = fe2_ini_section_find(&ini, "strings");
    if (!section) goto done;
    file = fe2_ini_get(section, "entries");
    count_text = fe2_ini_get(section, "count");
    dir = fe2_ini_get(section, "directory_offset");
    end_text = fe2_ini_get(section, "end_offset");
    if (!file || !count_text || !dir || !end_text) goto done;
    {
        const char *slash = strrchr(path, '/');
        const char *back = strrchr(path, '\\');
        const char *cut = !slash ? back : !back ? slash : (slash > back ? slash : back);
        size_t n = cut ? (size_t)(cut - path) : 0;
        if (!n || n >= sizeof base) goto done;
        memcpy(base, path, n);
        base[n] = 0;
    }
    if (fe2_path_join(entries, sizeof entries, base, file)) goto done;
    text = fe2_read_text(entries, NULL);
    if (!text) { fe2_asset_fail("Could not read %s", entries); goto done; }
    cursor = text;
    while (*cursor) {
        char *eol = strchr(cursor, '\n');
        char *tab;
        fe2_buf entry;
        if (eol) *eol = 0;
        if (cursor[0] && cursor[strlen(cursor) - 1] == '\r') cursor[strlen(cursor) - 1] = 0;
        tab = strchr(cursor, '\t');
        if (!tab || tab == cursor || count == 31) goto done;
        *tab = 0;
        if (!ident_ok(cursor) || strlen(cursor) >= 64) goto done;
        for (i = 0; i < count; ++i) if (!strcmp(names[i], cursor)) goto done;
        memcpy(names[count], cursor, strlen(cursor) + 1);
        memset(&entry, 0, sizeof entry);
        if (decode_text(tab + 1, &entry)) { fe2_buf_free(&entry); goto done; }
        for (i = 0; i < entry.count; ++i) if (!entry.data[i]) { fe2_buf_free(&entry); goto done; }
        if (fe2_buf_add(&strings, entry.data, entry.count) || fe2_buf_byte(&strings, 0)) {
            fe2_buf_free(&entry);
            goto done;
        }
        fe2_buf_free(&entry);
        ++count;
        if (!eol) break;
        cursor = eol + 1;
        if (!*cursor) break;
    }
    if (parse_i64(count_text, &expected) || expected != 31 || count != 31) goto done;
    if (parse_i64(dir, &directory) || parse_i64(end_text, &end_offset) || directory != 0xa3) goto done;
    at = count * 2u;
    for (i = 0; i < count; ++i) {
        offsets[i] = (unsigned)at;
        while (used < strings.count && strings.data[used]) ++used;
        ++used;
        at = count * 2u + used;
    }
    for (i = 0; i < count; ++i) if (fe2_buf_u16(out, offsets[i])) goto done;
    if (fe2_buf_add(out, strings.data, strings.count)) goto done;
    if (0xa3 + (long long)out->count != end_offset) goto done;
    status = 0;
done:
    free(text);
    fe2_buf_free(&strings);
    fe2_ini_free(&ini);
    if (status) fe2_asset_fail("market text layout changed");
    return status;
}

static int csv_integers(const fe2_csv *csv, long long **out, size_t *out_n)
{
    size_t start = 0, r, c, n = 0, at = 0;
    long long *values, dummy;
    int header = 0;
    for (c = 0; c < csv->rows[0].n; ++c) {
        const char *cell = csv->rows[0].cell[c];
        if (cell[0] && parse_i64(cell, &dummy)) header = 1;
    }
    if (header) start = 1;
    for (r = start; r < csv->n; ++r)
        for (c = 0; c < csv->rows[r].n; ++c)
            if (csv->rows[r].cell[c][0]) ++n;
    values = (long long *)calloc(n ? n : 1, sizeof *values);
    if (!values) return -1;
    for (r = start; r < csv->n; ++r) {
        for (c = 0; c < csv->rows[r].n; ++c) {
            const char *cell = csv->rows[r].cell[c];
            if (!cell[0]) continue;
            if (parse_i64(cell, &values[at])) { free(values); return fe2_asset_fail("%s is not an integer", cell); }
            ++at;
        }
    }
    *out = values;
    *out_n = n;
    return 0;
}

static int pack_words(fe2_buf *out, const long long *values, size_t n, unsigned width)
{
    size_t i;
    unsigned limit = width >= 4 ? 0xffffffffu : (1u << (width * 8u)) - 1u;
    for (i = 0; i < n; ++i) {
        unsigned value;
        if (values[i] < 0 || (unsigned long long)values[i] > limit) return -1;
        value = (unsigned)values[i];
        if (width == 1) { if (fe2_buf_byte(out, (uint8_t)value)) return -1; }
        else if (width == 2) { if (fe2_buf_u16(out, value)) return -1; }
        else {
            uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
            if (fe2_buf_add(out, bytes, 4)) return -1;
        }
    }
    return 0;
}

static int q15_rows(const fe2_csv *csv, const char *index_name, const char *const *labels, size_t label_n, fe2_buf *out)
{
    const char *header[] = {index_name, "coefficient"};
    size_t row;
    if (csv_header(csv, header, 2) || csv->n != label_n + 1) return -1;
    for (row = 0; row < label_n; ++row) {
        long long value;
        if (csv->rows[row + 1].n != 2 || strcmp(csv->rows[row + 1].cell[0], labels[row]) ||
            parse_dec(csv->rows[row + 1].cell[1], &value) || value < 0 || value > 32767 ||
            fe2_buf_u16(out, (unsigned)value)) return -1;
    }
    return 0;
}

static int commodity_words(const fe2_csv *csv, const char names[31][64], fe2_buf *out)
{
    static const char *header[] = {"commodity", "base_price", "price_variation", "base_supply", "supply_variation"};
    size_t row, col;
    if (csv_header(csv, header, 5) || csv->n != 32) return -1;
    for (row = 0; row < 31; ++row) {
        if (csv->rows[row + 1].n != 5 || strcmp(csv->rows[row + 1].cell[0], names[row])) return -1;
        for (col = 1; col < 5; ++col) {
            long long value;
            if (parse_i64(csv->rows[row + 1].cell[col], &value) || value < -32768 || value > 32767 ||
                fe2_buf_u16(out, (unsigned)(value & 0xffff))) return -1;
        }
    }
    return 0;
}

static int passage_words(const fe2_names *names, const fe2_csv *csv, fe2_buf *out)
{
    static const char *header[] = {"passage", "reward_multiplier"};
    size_t row;
    if (csv_header(csv, header, 2) || csv->n < 2) return -1;
    for (row = 1; row < csv->n; ++row) {
        unsigned selector;
        long long factor;
        if (csv->rows[row].n != 2 || strncmp(csv->rows[row].cell[0], "interaction:", 12) ||
            resolve_text(names, csv->rows[row].cell[0], &selector) ||
            parse_dec(csv->rows[row].cell[1], &factor) || factor < 0 || factor > 65535 ||
            fe2_buf_u16(out, selector) || fe2_buf_u16(out, (unsigned)factor)) return -1;
    }
    return 0;
}

static int reward_words(const fe2_csv *csv, fe2_buf *out)
{
    static const char *header[] = {"reward"};
    size_t row;
    if (csv_header(csv, header, 1) || csv->n < 2) return -1;
    for (row = 1; row < csv->n; ++row) {
        long long value;
        if (csv->rows[row].n != 1 || parse_dec(csv->rows[row].cell[0], &value) || value < 0 || value > 65535 ||
            fe2_buf_u16(out, (unsigned)value)) return -1;
    }
    return 0;
}

static int remote_words(const fe2_names *names, const fe2_csv *csv, fe2_buf *out)
{
    static const char *header[] = {"reward", "subject"};
    unsigned base;
    size_t row;
    if (csv_header(csv, header, 2) || csv->n < 2 || resolve_text(names, "interaction:text_empty_413", &base)) return -1;
    for (row = 1; row < csv->n; ++row) {
        unsigned selector;
        long long reward, offset;
        if (csv->rows[row].n != 2 || strncmp(csv->rows[row].cell[1], "interaction:", 12) ||
            resolve_text(names, csv->rows[row].cell[1], &selector) ||
            parse_dec(csv->rows[row].cell[0], &reward) || reward < 0 || reward > 65535) return -1;
        offset = (long long)selector - (long long)base;
        if (offset < 0 || offset > 65535 || fe2_buf_u16(out, (unsigned)reward) || fe2_buf_u16(out, (unsigned)offset))
            return -1;
    }
    return 0;
}

static int place_slots(uint8_t *image, size_t cap, const fe2_csv *csv, const char *const *bank_names,
                       const unsigned *banks, size_t bank_n)
{
    static const char *header[] = {"bank", "slot", "x", "y", "z"};
    size_t b, row;
    if (csv_header(csv, header, 5)) return -1;
    for (row = 1; row < csv->n; ++row) if (csv->rows[row].n != 5) return -1;
    for (b = 0; b < bank_n; ++b) {
        unsigned base = banks[b * 2];
        unsigned count = banks[b * 2 + 1];
        unsigned slot = 0;
        for (row = 1; row < csv->n; ++row) {
            long long got, x, y, z;
            if (csv->rows[row].n != 5) return -1;
            if (strcmp(csv->rows[row].cell[0], bank_names[b])) continue;
            if (csv->rows[row].n != 5 || parse_dec(csv->rows[row].cell[1], &got) || got != (long long)slot) return -1;
            if (parse_dec(csv->rows[row].cell[2], &x) || parse_dec(csv->rows[row].cell[3], &y) ||
                parse_dec(csv->rows[row].cell[4], &z)) return -1;
            if (x < -32768 || x > 32767 || y < -32768 || y > 32767 || z < -32768 || z > 32767) return -1;
            {
                size_t at = (size_t)base + (size_t)slot * 8u;
                unsigned words[3] = {(unsigned)(x & 0xffff), (unsigned)(y & 0xffff), (unsigned)(z & 0xffff)};
                size_t part;
                if (at + 6 > cap) return -1;
                for (part = 0; part < 3; ++part) {
                    image[at + 2 * part] = (uint8_t)words[part];
                    image[at + 2 * part + 1] = (uint8_t)(words[part] >> 8);
                }
            }
            ++slot;
        }
        if (slot != count) return -1;
    }
    for (row = 1; row < csv->n; ++row) {
        int known = 0;
        for (b = 0; b < bank_n; ++b) if (!strcmp(csv->rows[row].cell[0], bank_names[b])) known = 1;
        if (!known) return -1;
    }
    return 0;
}

static int attachment_bytes(const fe2_names *names, const fe2_csv *slots, const fe2_csv *offers, fe2_buf *out)
{
    static const char *header[] = {"offer", "model"};
    uint8_t image[0x124];
    unsigned counts[13];
    size_t row, i;
    memset(image, 0, sizeof image);
    memset(counts, 0, sizeof counts);
    if (place_slots(image, sizeof image, slots, attachment_bank_names, attachment_banks, 3)) return -1;
    if (csv_header(offers, header, 2)) return -1;
    for (row = 1; row < offers->n; ++row) {
        int found = -1;
        unsigned model;
        if (offers->rows[row].n != 2) return -1;
        for (i = 0; i < 13; ++i) if (!strcmp(offers->rows[row].cell[0], offer_names[i])) found = (int)i;
        if (found < 0 || parse_model(names, offers->rows[row].cell[1], &model)) return -1;
        if (counts[found] >= offer_count[found]) return -1;
        {
            size_t at = (size_t)offer_at[found] + (size_t)counts[found] * 2u;
            if (at + 2 > sizeof image) return -1;
            image[at] = (uint8_t)model;
            image[at + 1] = (uint8_t)(model >> 8);
        }
        ++counts[found];
    }
    for (i = 0; i < 13; ++i) if (counts[i] != offer_count[i]) return -1;
    return fe2_buf_add(out, image, sizeof image);
}

static int category_words(const fe2_names *names, const fe2_csv *offers, fe2_buf *out)
{
    static const char *header[] = {"offer", "model"};
    unsigned counts[13];
    size_t row, i;
    memset(counts, 0, sizeof counts);
    if (csv_header(offers, header, 2)) return -1;
    for (row = 1; row < offers->n; ++row) {
        int found = -1;
        unsigned model;
        if (offers->rows[row].n != 2) return -1;
        for (i = 0; i < 13; ++i) if (!strcmp(offers->rows[row].cell[0], offer_names[i])) found = (int)i;
        if (found < 0 || parse_model(names, offers->rows[row].cell[1], &model)) return -1;
        ++counts[found];
    }
    for (i = 0; i < 13; ++i) {
        if (counts[i] != offer_count[i]) return -1;
        if (fe2_buf_u16(out, counts[i]) || fe2_buf_u16(out, 0x0443u + offer_at[i])) return -1;
    }
    return 0;
}

static int command_bytes(const fe2_csv *csv, fe2_buf *out)
{
    uint8_t image[0x78];
    memset(image, 0, sizeof image);
    if (place_slots(image, sizeof image, csv, command_bank_names, command_banks, 4)) return -1;
    return fe2_buf_add(out, image, sizeof image);
}

static int control_bytes(const fe2_csv *csv, fe2_buf *out)
{
    uint8_t image[0x3c];
    memset(image, 0, sizeof image);
    if (place_slots(image, sizeof image, csv, control_bank_names, control_banks, 2)) return -1;
    return fe2_buf_add(out, image, sizeof image);
}

static int allegiance_words(fe2_names *names, const char *root, const fe2_csv *csv, fe2_buf *out)
{
    static const char *header[] = {"allegiance", "system", "sector_x", "sector_y", "number"};
    int coord_x[47], coord_y[47];
    unsigned bounds[50];
    char label[64], expected[64];
    size_t row;
    if (csv_header(csv, header, 5) || csv->n != 4 || catalog_layout(root, coord_x, coord_y, bounds)) return -1;
    for (row = 0; row < 3; ++row) {
        long long sx, sy, number;
        long long map_x, map_y;
        uint32_t key;
        uint8_t bytes[4];
        if (csv->rows[row + 1].n != 5 || allegiance_label(names, root, (unsigned)row, label, sizeof label) ||
            strcmp(csv->rows[row + 1].cell[0], label) ||
            parse_dec(csv->rows[row + 1].cell[2], &sx) || parse_dec(csv->rows[row + 1].cell[3], &sy) ||
            parse_dec(csv->rows[row + 1].cell[4], &number)) return -1;
        if (map_name(names, root, (int)sx, (int)sy, (int)number, coord_x, coord_y, bounds, expected, sizeof expected) ||
            strcmp(csv->rows[row + 1].cell[1], expected)) return -1;
        map_x = sx + CENTRE_X;
        map_y = sy + CENTRE_Y;
        key = ((uint32_t)map_y << 19) | ((uint32_t)map_x << 6) | (uint32_t)number;
        bytes[0] = (uint8_t)key;
        bytes[1] = (uint8_t)(key >> 8);
        bytes[2] = (uint8_t)(key >> 16);
        bytes[3] = (uint8_t)(key >> 24);
        if (fe2_buf_add(out, bytes, 4)) return -1;
    }
    return 0;
}

static int dispatch_words(const fe2_csv *csv, size_t count, fe2_buf *out)
{
    static const char *header[] = {"slot", "handler"};
    size_t row, h;
    if (csv_header(csv, header, 2) || csv->n != count + 1) return -1;
    for (row = 0; row < count; ++row) {
        long long slot;
        int found = 0;
        if (csv->rows[row + 1].n != 2 || parse_i64(csv->rows[row + 1].cell[0], &slot) || slot != (long long)row)
            return -1;
        for (h = 0; h < sizeof handlers / sizeof handlers[0]; ++h) {
            if (!strcmp(csv->rows[row + 1].cell[1], handlers[h])) {
                if (fe2_buf_u16(out, handler_at[h])) return -1;
                found = 1;
                break;
            }
        }
        if (!found) return -1;
    }
    return 0;
}

static int place(uint8_t *image, fe2_span *spans, size_t *span_n, unsigned start, unsigned end, const fe2_buf *blob)
{
    size_t i;
    if (start < MARKET_BASE || end > MARKET_END || start >= end || blob->count != (size_t)(end - start)) return -1;
    for (i = 0; i < *span_n; ++i) if (start < spans[i].end && spans[i].begin < end) return -1;
    memcpy(image + (start - MARKET_BASE), blob->data, blob->count);
    if (*span_n == 32) return -1;
    spans[*span_n].begin = start;
    spans[*span_n].end = end;
    ++*span_n;
    return 0;
}

static int covered(fe2_span *spans, size_t n)
{
    size_t i, j;
    for (i = 0; i < n; ++i) for (j = i + 1; j < n; ++j) if (spans[j].begin < spans[i].begin) {
        fe2_span swap = spans[i];
        spans[i] = spans[j];
        spans[j] = swap;
    }
    if (!n || spans[0].begin != MARKET_BASE || spans[n - 1].end != MARKET_END) return -1;
    for (i = 1; i < n; ++i) if (spans[i - 1].end != spans[i].begin) return -1;
    return 0;
}

static const char *index_labels(const char *index, const char ***labels, size_t *count)
{
    static char strength[8][2];
    static int ready = 0;
    size_t i;
    if (!ready) {
        for (i = 0; i < 8; ++i) { strength[i][0] = (char)('0' + (int)i); strength[i][1] = 0; }
        ready = 1;
    }
    if (!strcmp(index, "strength") || !strcmp(index, "economic_factor")) {
        static const char *out[8];
        for (i = 0; i < 8; ++i) out[i] = strength[i];
        *labels = out;
        *count = 8;
        return NULL;
    }
    if (!strcmp(index, "population")) {
        *labels = population_labels;
        *count = 10;
        return NULL;
    }
    return "index";
}

int fe2_compile_market_image(const char *data_root, uint8_t **out, size_t *out_n)
{
    uint8_t *image = NULL;
    fe2_names *names = NULL;
    fe2_ini ini;
    fe2_buf text, blob;
    fe2_span spans[32];
    size_t span_n = 0, section;
    char commodity[31][64];
    char path[4096];
    int status = -1;
    if (!data_root || !out || !out_n) return -1;
    *out = NULL;
    *out_n = 0;
    memset(&ini, 0, sizeof ini);
    memset(&text, 0, sizeof text);
    memset(&blob, 0, sizeof blob);
    memset(commodity, 0, sizeof commodity);
    image = (uint8_t *)calloc(MARKET_BYTES, 1);
    names = (fe2_names *)calloc(1, sizeof *names);
    if (!image || !names) goto done;
    image[0] = 0xff;
    if (load_models(names, data_root) || load_texts(names, data_root) ||
        compile_market_text(data_root, &text, commodity)) goto done;
    spans[0].begin = MARKET_BASE;
    spans[0].end = 0xa3u + (unsigned)text.count;
    if (spans[0].end > MARKET_END || text.count + 1u > MARKET_BYTES) goto done;
    memcpy(image + 1, text.data, text.count);
    span_n = 1;
    if (under(data_root, "economy/tables.ini", path, sizeof path) || fe2_ini_load(&ini, path)) {
        fe2_asset_fail("Could not read %s", path);
        goto done;
    }
    for (section = 0; section < ini.count; ++section) {
        const fe2_ini_section *item = &ini.sections[section];
        const char *kind = fe2_ini_get(item, "type");
        const char *file = fe2_ini_get(item, "file");
        const char *start_text = fe2_ini_get(item, "start");
        const char *end_text = fe2_ini_get(item, "end");
        long long start, end;
        char file_path[4096];
        fe2_csv csv;
        memset(&csv, 0, sizeof csv);
        fe2_buf_free(&blob);
        if (!kind || !file || !start_text || !end_text || parse_i64(start_text, &start) || parse_i64(end_text, &end) ||
            start < 0 || end < start || end > 0xffffffffll) goto done;
        if (snprintf(file_path, sizeof file_path, "%s/economy/%s", data_root, file) >= (int)sizeof file_path ||
            csv_load(file_path, &csv)) goto done;
        if (!strcmp(kind, "u8") || !strcmp(kind, "u16") || !strcmp(kind, "u32")) {
            long long *values = NULL;
            size_t n = 0;
            unsigned width = kind[1] == '8' ? 1u : kind[1] == '1' ? 2u : 4u;
            if (csv_integers(&csv, &values, &n) || pack_words(&blob, values, n, width)) { free(values); csv_free(&csv); goto done; }
            free(values);
        } else if (!strcmp(kind, "q15_coefficients")) {
            const char *index = fe2_ini_get(item, "index");
            const char **labels = NULL;
            size_t label_n = 0;
            if (!index || index_labels(index, &labels, &label_n) || q15_rows(&csv, index, labels, label_n, &blob)) {
                csv_free(&csv); goto done;
            }
        } else if (!strcmp(kind, "commodity_parameters")) {
            if (commodity_words(&csv, commodity, &blob)) { csv_free(&csv); goto done; }
        } else if (!strcmp(kind, "passage_pairs")) {
            if (passage_words(names, &csv, &blob)) { csv_free(&csv); goto done; }
        } else if (!strcmp(kind, "docking_slots")) {
            const char *offers = fe2_ini_get(item, "offers");
            char offers_path[4096];
            fe2_csv offer_csv;
            memset(&offer_csv, 0, sizeof offer_csv);
            if (!offers || snprintf(offers_path, sizeof offers_path, "%s/economy/%s", data_root, offers) >= (int)sizeof offers_path ||
                csv_load(offers_path, &offer_csv) || attachment_bytes(names, &csv, &offer_csv, &blob)) {
                csv_free(&offer_csv); csv_free(&csv); goto done;
            }
            csv_free(&offer_csv);
        } else if (!strcmp(kind, "category_offers")) {
            if (category_words(names, &csv, &blob)) { csv_free(&csv); goto done; }
        } else if (!strcmp(kind, "command_slots")) {
            if (command_bytes(&csv, &blob)) { csv_free(&csv); goto done; }
        } else if (!strcmp(kind, "control_slots")) {
            if (control_bytes(&csv, &blob)) { csv_free(&csv); goto done; }
        } else if (!strcmp(kind, "allegiance_systems")) {
            if (allegiance_words(names, data_root, &csv, &blob)) { csv_free(&csv); goto done; }
        } else if (!strcmp(kind, "reward_words")) {
            if (reward_words(&csv, &blob)) { csv_free(&csv); goto done; }
        } else if (!strcmp(kind, "remote_subjects")) {
            if (remote_words(names, &csv, &blob)) { csv_free(&csv); goto done; }
        } else {
            csv_free(&csv);
            fe2_asset_fail("Unsupported table type");
            goto done;
        }
        csv_free(&csv);
        if (place(image, spans, &span_n, (unsigned)start, (unsigned)end, &blob)) {
            fe2_asset_fail("Table size changed: %s", item->name);
            goto done;
        }
    }
    {
        static const struct { unsigned at; const char *file; size_t count; } dispatch[] = {
            {0x3a1, "market_record_dispatch.csv", 32},
            {0x59b, "system_record_dispatch_table.csv", 16}
        };
        size_t d;
        for (d = 0; d < 2; ++d) {
            char file_path[4096];
            fe2_csv csv;
            memset(&csv, 0, sizeof csv);
            fe2_buf_free(&blob);
            if (snprintf(file_path, sizeof file_path, "%s/economy/%s", data_root, dispatch[d].file) >= (int)sizeof file_path ||
                csv_load(file_path, &csv) || dispatch_words(&csv, dispatch[d].count, &blob) ||
                place(image, spans, &span_n, dispatch[d].at, dispatch[d].at + (unsigned)blob.count, &blob)) {
                csv_free(&csv);
                goto done;
            }
            csv_free(&csv);
        }
    }
    if (covered(spans, span_n)) goto done;
    *out = image;
    *out_n = MARKET_BYTES;
    image = NULL;
    status = 0;
done:
    free(image);
    names_free(names);
    free(names);
    fe2_buf_free(&text);
    fe2_buf_free(&blob);
    fe2_ini_free(&ini);
    if (status) fe2_asset_fail("market resources must cover 00A2h..06EAh exactly");
    return status;
}

enum { SYSTEMS_BASE = 0xa2, SYSTEMS_END = 0x3f21, SYSTEMS_BYTES = SYSTEMS_END - SYSTEMS_BASE };

static int put_le(uint8_t *image, size_t cap, size_t at, unsigned value, unsigned width)
{
    unsigned i;
    if (at + width > cap) return -1;
    for (i = 0; i < width; ++i) image[at + i] = (uint8_t)(value >> (8u * i));
    return 0;
}

static int compile_model_ids(const fe2_names *names, const fe2_csv *csv, fe2_buf *out)
{
    size_t r, c;
    for (r = 0; r < csv->n; ++r) for (c = 0; c < csv->rows[r].n; ++c) {
        unsigned model;
        if (!csv->rows[r].cell[c][0]) continue;
        if (parse_model(names, csv->rows[r].cell[c], &model) || fe2_buf_u16(out, model)) return -1;
    }
    return 0;
}

static int compile_record(const fe2_names *names, const fe2_ini_section *section, const fe2_csv *csv, fe2_buf *out)
{
    const char *fields_text = fe2_ini_get(section, "fields");
    const char *columns_text = fe2_ini_get(section, "columns");
    char fields[16][16];
    char columns[16][64];
    size_t field_n = 0, column_n = 0, row, f;
    const char *cursor;
    if (!fields_text) return -1;
    cursor = fields_text;
    while (*cursor && field_n < 16) {
        size_t n = 0;
        while (*cursor == ' ') ++cursor;
        while (*cursor && *cursor != ',') {
            if (n + 1 >= sizeof fields[field_n]) return -1;
            fields[field_n][n++] = *cursor++;
        }
        fields[field_n][n] = 0;
        if (!n) return -1;
        ++field_n;
        if (*cursor == ',') ++cursor;
    }
    if (*cursor || !field_n) return -1;
    if (columns_text) {
        cursor = columns_text;
        while (*cursor && column_n < 16) {
            size_t n = 0;
            while (*cursor == ' ') ++cursor;
            while (*cursor && *cursor != ',') {
                if (n + 1 >= sizeof columns[column_n]) return -1;
                columns[column_n][n++] = *cursor++;
            }
            columns[column_n][n] = 0;
        if (!n) return -1;
        ++column_n;
        if (*cursor == ',') ++cursor;
    }
    if (*cursor || column_n != field_n) return -1;
}
if (columns_text) {
    const char *header[16];
    if (column_n != field_n) return -1;
    for (f = 0; f < column_n; ++f) header[f] = columns[f];
    if (csv_header(csv, header, column_n)) return -1;
}
    for (row = columns_text ? 1 : 0; row < csv->n; ++row) {
        if (csv->rows[row].n != field_n) return -1;
        for (f = 0; f < field_n; ++f) {
            const char *raw = csv->rows[row].cell[f];
            const char *field = fields[f];
            long long value;
            unsigned model, width;
            if (!strcmp(field, "u8")) width = 1;
            else if (!strcmp(field, "u16")) width = 2;
            else if (!strcmp(field, "u32")) width = 4;
            else if (!strcmp(field, "model")) width = 2;
            else return -1;
            if (!strcmp(field, "model")) {
                if (parse_model(names, raw, &model)) return -1;
                value = model;
            } else if (parse_i64(raw, &value) || value < 0) return -1;
            if ((unsigned long long)value >= (width >= 4 ? 0x100000000ull : 1ull << (width * 8u))) return -1;
            if (width == 1) { if (fe2_buf_byte(out, (uint8_t)value)) return -1; }
            else if (width == 2) { if (fe2_buf_u16(out, (unsigned)value)) return -1; }
            else {
                uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
                if (fe2_buf_add(out, bytes, 4)) return -1;
            }
        }
    }
    return csv->n > (columns_text ? 1 : 0) ? 0 : -1;
}

static int compile_object(const fe2_names *names, const char *path, fe2_buf *out)
{
    static const struct { unsigned offset, width, count; const char *name; } fields[] = {
        {0x00, 2, 9, "orientation"}, {0x12, 1, 1, "rebuild_budget"}, {0x13, 1, 1, "render_state"},
        {0x14, 4, 3, "camera_offset"}, {0x20, 4, 3, "camera_target"}, {0x2c, 4, 3, "position_low"},
        {0x38, 4, 3, "position_high"}, {0x44, 1, 1, "parent_handle"}, {0x45, 1, 1, "station_slot"},
        {0x46, 2, 9, "alternate_orientation"}, {0x58, 2, 1, "model_parameter"}, {0x5a, 2, 1, "model"},
        {0x5c, 1, 1, "basis_generation"}, {0x5d, 1, 1, "handle"}, {0x5e, 1, 1, "role"},
        {0x5f, 1, 1, "level"}, {0x60, 1, 1, "update_period"}, {0x61, 1, 1, "update_countdown"},
        {0x62, 4, 3, "velocity"}, {0x6e, 2, 1, "screen_x"}, {0x70, 2, 1, "screen_y"},
        {0x72, 2, 1, "clock"}, {0x74, 2, 1, "cargo_type"}, {0x76, 4, 1, "target_seed"},
        {0x7a, 2, 1, "size_mantissa"}, {0x7c, 2, 1, "size_exponent"}, {0x7e, 4, 2, "orbit"},
        {0x86, 4, 1, "scalar"}, {0x8a, 2, 3, "vector_delta"}, {0x90, 2, 4, "orbit_product"},
        {0x98, 4, 1, "orbit_radius"}, {0x9c, 2, 1, "orbit_inclination"}, {0x9e, 1, 1, "time_shift"},
        {0x9f, 1, 1, "docking_state"}, {0xa0, 1, 1, "docking_claim"}, {0xa1, 1, 1, "generation"},
        {0xa2, 2, 1, "basis_seed"}, {0xa4, 2, 1, "shape"}, {0xa6, 1, 8, "docked"},
        {0xae, 2, 1, "docking_timer"}, {0xb0, 2, 1, "scale_exponent"}, {0xb2, 2, 1, "radius_class"},
        {0xb4, 2, 1, "progress"}, {0xb6, 2, 1, "temperature"}, {0xb8, 2, 1, "hull"},
        {0xba, 2, 1, "stored_model"}, {0xbc, 2, 1, "cooldown"}, {0xbe, 2, 1, "busy_remaining"},
        {0xc0, 2, 3, "velocity_fraction"}, {0xc6, 4, 3, "feedback"}, {0xd2, 1, 1, "behaviour_parent"},
        {0xd3, 1, 1, "behaviour"}, {0xd4, 1, 1, "behaviour_target"}, {0xd5, 1, 1, "control_class"},
        {0xd6, 2, 3, "source_offset"}, {0xdc, 2, 3, "steering"}, {0xe2, 2, 1, "steering_shift"},
        {0xe4, 2, 3, "rotation_rates"}, {0xea, 2, 1, "capacity"}, {0xec, 1, 1, "map_marker"},
        {0xed, 1, 1, "drive_fuel"}, {0xee, 4, 1, "reward"}, {0xf2, 4, 1, "fuel"},
        {0xf6, 1, 20, "name"}, {0x10a, 2, 3, "model_scale_a"}, {0x110, 2, 3, "model_scale_b"},
        {0x116, 2, 1, "transition_countdown"}, {0x118, 2, 1, "flags"}, {0x11a, 1, 1, "trail"},
        {0x11b, 1, 1, "effect"}, {0x11c, 1, 1, "probe_countdown"}, {0x11d, 1, 1, "frame_state"}
    };
    uint8_t record[0x11e];
    char *text, *cursor;
    unsigned char seen[sizeof fields / sizeof fields[0]];
    memset(record, 0, sizeof record);
    memset(seen, 0, sizeof seen);
    text = fe2_read_text(path, NULL);
    if (!text) return fe2_asset_fail("Could not read %s", path);
    cursor = text;
    while (*cursor) {
        char *eol = strchr(cursor, '\n');
        char *hash, *eq;
        size_t f, n;
        if (eol) *eol = 0;
        if (cursor[0] && cursor[strlen(cursor) - 1] == '\r') cursor[strlen(cursor) - 1] = 0;
        hash = strchr(cursor, '#');
        if (hash) *hash = 0;
        trim(cursor);
        if (cursor[0]) {
            eq = strchr(cursor, '=');
            if (!eq) { free(text); return -1; }
            *eq = 0;
            trim(cursor);
            trim(eq + 1);
            for (f = 0; f < sizeof fields / sizeof fields[0]; ++f) if (!strcmp(fields[f].name, cursor)) break;
            if (f == sizeof fields / sizeof fields[0] || seen[f]) { free(text); return -1; }
            seen[f] = 1;
            if (!strcmp(fields[f].name, "name")) {
                n = strlen(eq + 1);
                if (!n || n >= fields[f].count) { free(text); return -1; }
                for (n = 0; eq[1 + n]; ++n) if ((unsigned char)eq[1 + n] < 32 || (unsigned char)eq[1 + n] >= 127) {
                    free(text); return -1;
                }
                memcpy(record + fields[f].offset, eq + 1, strlen(eq + 1));
            } else {
                char *part = eq + 1;
                unsigned index = 0;
                while (*part) {
                    char *comma = strchr(part, ',');
                    long long value;
                    unsigned model;
                    if (comma) *comma = 0;
                    trim(part);
                    if (index >= fields[f].count) { free(text); return -1; }
                    if ((!strcmp(fields[f].name, "orientation") || !strcmp(fields[f].name, "alternate_orientation")) &&
                        !strcmp(part, "near_unit")) value = 0x7f00;
                    else if ((!strcmp(fields[f].name, "model") || !strcmp(fields[f].name, "stored_model"))) {
                        if (parse_model(names, part, &model)) { free(text); return -1; }
                        value = model;
                    } else if (parse_i64(part, &value) || value < 0) { free(text); return -1; }
                    if ((unsigned long long)value >= 1ull << (8u * fields[f].width) ||
                        put_le(record, sizeof record, fields[f].offset + index * fields[f].width,
                               (unsigned)value, fields[f].width)) { free(text); return -1; }
                    ++index;
                    if (!comma) break;
                    part = comma + 1;
                }
                if (index != fields[f].count) { free(text); return -1; }
            }
        }
        if (!eol) break;
        cursor = eol + 1;
    }
    free(text);
    return fe2_buf_add(out, record, sizeof record);
}

static int body_kind(const char *token, unsigned *out)
{
    static const struct { const char *name; unsigned kind; } kinds[] = {
        {"root", 0}, {"large_shape_7", 0x44}, {"verbatim", 0x46}
    };
    size_t i;
    long long value;
    for (i = 0; i < 3; ++i) if (!strcmp(token, kinds[i].name)) { *out = kinds[i].kind; return 0; }
    if (parse_dec(token, &value) || value < 0 || value > 0xffff) return -1;
    for (i = 0; i < 3; ++i) if (kinds[i].kind == (unsigned)value) return -1;
    *out = (unsigned)value;
    return 0;
}

static int compile_template(const fe2_names *names, const fe2_csv *csv, fe2_buf *out)
{
    static const char *header[] = {
        "size", "seed", "parent", "kind", "descriptor", "scalar", "orbit_longitude",
        "orbit_eccentricity", "orbit_inclination", "temperature_c", "name",
        "orbit_shift", "generation", "basis_seed"
    };
    size_t row, i, count;
    if (csv_header(csv, header, 14) || csv->n < 2) return -1;
    count = csv->n - 1;
    for (row = 0; row < count; ++row) {
        const char *name = csv->rows[row + 1].cell[10];
        if (csv->rows[row + 1].n != 14 || !strcmp(name, "frontier") || !name[0] || strlen(name) >= 22) return -1;
        for (i = 0; name[i]; ++i) if ((unsigned char)name[i] < 32 || (unsigned char)name[i] >= 127) return -1;
        for (i = 0; i < row; ++i) if (!strcmp(csv->rows[i + 1].cell[10], name)) return -1;
    }
    for (row = 0; row < count; ++row) {
        const fe2_csv_row *item = &csv->rows[row + 1];
        uint8_t record[64];
        long long size, seed, scalar, longitude, eccentricity, inclination, basis, celsius, shift, generation;
        unsigned kind, model, parent = 0;
        int found = 0;
        memset(record, 0, sizeof record);
        if (parse_i64(item->cell[0], &size) || size < 0 || size > 0xffffffffll ||
            parse_i64(item->cell[1], &seed) || seed < 0 || seed > 0xffffffffll ||
            parse_i64(item->cell[5], &scalar) || scalar < 0 || scalar > 0xffffffffll ||
            parse_i64(item->cell[6], &longitude) || longitude < 0 || longitude > 0xffff ||
            parse_i64(item->cell[7], &eccentricity) || eccentricity < 0 || eccentricity > 0xffff ||
            parse_i64(item->cell[8], &inclination) || inclination < 0 || inclination > 0xffff ||
            parse_i64(item->cell[13], &basis) || basis < 0 || basis > 0xffff ||
            body_kind(item->cell[3], &kind) || parse_model(names, item->cell[4], &model) ||
            parse_dec(item->cell[9], &celsius) || celsius < -32768 || celsius > 32767 ||
            celsius + 273 < 0 || celsius + 273 > 65535 ||
            parse_dec(item->cell[11], &shift) || shift < 0 || shift > 255 ||
            parse_dec(item->cell[12], &generation) || generation < 0 || generation > 255) return -1;
        if (!strcmp(item->cell[2], "frontier")) parent = 0;
        else {
            for (i = 0; i < count; ++i) if (!strcmp(csv->rows[i + 1].cell[10], item->cell[2])) {
                parent = (unsigned)i + 1u;
                found = 1;
            }
            if (!found) return -1;
        }
        if (put_le(record, 64, 0, (unsigned)size, 4) || put_le(record, 64, 4, (unsigned)seed, 4) ||
            put_le(record, 64, 8, parent, 2) || put_le(record, 64, 10, kind, 2) ||
            put_le(record, 64, 12, model, 2) || put_le(record, 64, 14, (unsigned)scalar, 4) ||
            put_le(record, 64, 18, (unsigned)longitude, 2) || put_le(record, 64, 28, (unsigned)eccentricity, 2) ||
            put_le(record, 64, 30, (unsigned)inclination, 2) ||
            put_le(record, 64, 32, (unsigned)(celsius + 273), 2) ||
            put_le(record, 64, 58, (unsigned)shift, 1) || put_le(record, 64, 59, (unsigned)generation, 1) ||
            put_le(record, 64, 62, (unsigned)basis, 2)) return -1;
        memcpy(record + 34, item->cell[10], strlen(item->cell[10]));
        if (fe2_buf_add(out, record, 64)) return -1;
    }
    return 0;
}

static int place_at(uint8_t *image, unsigned base, unsigned limit, fe2_span *spans, size_t *span_n,
                    unsigned start, const void *data, size_t count)
{
    size_t i;
    unsigned end = start + (unsigned)count;
    if (start < base || end > limit || start >= end) return -1;
    for (i = 0; i < *span_n; ++i) if (start < spans[i].end && spans[i].begin < end) return -1;
    memcpy(image + (start - base), data, count);
    if (*span_n == 64) return -1;
    spans[*span_n].begin = start;
    spans[*span_n].end = end;
    ++*span_n;
    return 0;
}

static int systems_covered(fe2_span *spans, size_t n)
{
    size_t i, j;
    for (i = 0; i < n; ++i) for (j = i + 1; j < n; ++j) if (spans[j].begin < spans[i].begin) {
        fe2_span swap = spans[i];
        spans[i] = spans[j];
        spans[j] = swap;
    }
    if (!n || spans[0].begin != SYSTEMS_BASE || spans[n - 1].end != SYSTEMS_END) return -1;
    for (i = 1; i < n; ++i) if (spans[i - 1].end != spans[i].begin) return -1;
    return 0;
}

static int compile_profiles(const fe2_names *names, const char *path, uint8_t *image, fe2_span *spans, size_t *span_n)
{
    static const char *header[] = {
        "profile", "size_threshold", "kind", "descriptor", "temperature",
        "subtree_position", "profile_reference", "rule"
    };
    static const struct { const char *name; unsigned at; } rules[] = {
        {"clear_profile_scalar_and_maybe_generate_name", 0x4bda}, {"settle_warm_body", 0x4bad},
        {"settle_moderate_temperature_body", 0x4b69}, {"warm_and_settle_body", 0x4abb},
        {"reclassify_temperature_and_settlement", 0x4990}, {"promote_profile_to_descriptor_ae", 0x4bff},
        {"promote_profile_after_two_negative_samples", 0x4c27}, {"promote_profile_after_negative_sample", 0x4c2c}
    };
    static const struct { unsigned at, width; } fields[] = {
        {0xa3, 2}, {0xd1, 2}, {0xff, 4}, {0x15b, 2}, {0x189, 2}, {0x1b7, 4}
    };
    fe2_csv csv;
    uint8_t columns[6][23 * 4];
    uint8_t callbacks[46];
    size_t row, rule;
    memset(&csv, 0, sizeof csv);
    memset(columns, 0, sizeof columns);
    if (csv_load(path, &csv) || csv_header(&csv, header, 8) || csv.n != 24) { csv_free(&csv); return -1; }
    for (row = 0; row < 23; ++row) {
        const fe2_csv_row *item = &csv.rows[row + 1];
        long long profile, values[6];
        unsigned model, callback = 0;
        int found = 0;
        size_t f;
        if (item->n != 8 || parse_i64(item->cell[0], &profile) || profile != (long long)row) { csv_free(&csv); return -1; }
        if (!strcmp(item->cell[7], "none")) callback = 0;
        else {
            for (rule = 0; rule < sizeof rules / sizeof rules[0]; ++rule) if (!strcmp(item->cell[7], rules[rule].name)) {
                callback = rules[rule].at;
                found = 1;
            }
            if (!found) { csv_free(&csv); return -1; }
        }
        callbacks[row * 2] = (uint8_t)callback;
        callbacks[row * 2 + 1] = (uint8_t)(callback >> 8);
        if (parse_i64(item->cell[1], &values[2]) || parse_i64(item->cell[2], &values[0]) ||
            parse_model(names, item->cell[3], &model) || parse_i64(item->cell[4], &values[4]) ||
            parse_i64(item->cell[5], &values[3]) || parse_i64(item->cell[6], &values[5])) { csv_free(&csv); return -1; }
        values[1] = model;
        for (f = 0; f < 6; ++f) {
            if (values[f] < 0 || (unsigned long long)values[f] >= (fields[f].width >= 4 ? 0x100000000ull : 1ull << (8u * fields[f].width))) {
                csv_free(&csv); return -1;
            }
            if (put_le(columns[f], sizeof columns[f], row * fields[f].width, (unsigned)values[f], fields[f].width)) {
                csv_free(&csv); return -1;
            }
        }
    }
    csv_free(&csv);
    if (place_at(image, SYSTEMS_BASE, SYSTEMS_END, spans, span_n, 0x213, callbacks, sizeof callbacks)) return -1;
    for (row = 0; row < 6; ++row)
        if (place_at(image, SYSTEMS_BASE, SYSTEMS_END, spans, span_n, fields[row].at, columns[row], 23u * fields[row].width))
            return -1;
    return 0;
}

static int compile_system_strings(const char *root, uint8_t *image, fe2_span *spans, size_t *span_n)
{
    fe2_ini ini;
    fe2_ini_section *section;
    char path[4096], base[4096], entries[4096];
    const char *file, *count_text, *dir_text, *end_text;
    long long count_expected, directory, end_offset;
    fe2_buf blob;
    int status = -1;
    memset(&ini, 0, sizeof ini);
    memset(&blob, 0, sizeof blob);
    if (under(root, "text/systems/catalog.ini", path, sizeof path) || fe2_ini_load(&ini, path)) goto done;
    section = fe2_ini_section_find(&ini, "strings");
    if (!section) goto done;
    file = fe2_ini_get(section, "entries");
    count_text = fe2_ini_get(section, "count");
    dir_text = fe2_ini_get(section, "directory_offset");
    end_text = fe2_ini_get(section, "end_offset");
    if (!file || !dir_text || !end_text || parse_i64(dir_text, &directory) || parse_i64(end_text, &end_offset) ||
        directory < 0 || end_offset < directory) goto done;
    {
        const char *slash = strrchr(path, '/');
        const char *back = strrchr(path, '\\');
        const char *cut = !slash ? back : !back ? slash : (slash > back ? slash : back);
        size_t n = cut ? (size_t)(cut - path) : 0;
        if (!n || n >= sizeof base) goto done;
        memcpy(base, path, n);
        base[n] = 0;
    }
    if (fe2_path_join(entries, sizeof entries, base, file)) goto done;
    if (!fe2_file_exists(entries)) {
        uint8_t *zero = (uint8_t *)calloc((size_t)(end_offset - directory), 1);
        if (!zero) goto done;
        status = place_at(image, SYSTEMS_BASE, SYSTEMS_END, spans, span_n, (unsigned)directory, zero,
                          (size_t)(end_offset - directory));
        free(zero);
        goto done;
    }
    {
        char *text = fe2_read_text(entries, NULL);
        char *cursor;
        fe2_buf strings;
        unsigned offsets[256];
        size_t count = 0, i, at, used = 0;
        memset(&strings, 0, sizeof strings);
        if (!text) goto done;
        cursor = text;
        while (*cursor && count < 256) {
            char *eol = strchr(cursor, '\n');
            char *tab;
            fe2_buf entry;
            if (eol) *eol = 0;
            if (cursor[0] && cursor[strlen(cursor) - 1] == '\r') cursor[strlen(cursor) - 1] = 0;
            tab = strchr(cursor, '\t');
            if (!tab || tab == cursor) { free(text); fe2_buf_free(&strings); goto done; }
            memset(&entry, 0, sizeof entry);
            if (decode_text(tab + 1, &entry) || fe2_buf_add(&strings, entry.data, entry.count) || fe2_buf_byte(&strings, 0)) {
                free(text); fe2_buf_free(&entry); fe2_buf_free(&strings); goto done;
            }
            fe2_buf_free(&entry);
            ++count;
            if (!eol) break;
            cursor = eol + 1;
            if (!*cursor) break;
        }
        free(text);
        if (count_text && (parse_i64(count_text, &count_expected) || count_expected != (long long)count)) {
            fe2_buf_free(&strings); goto done;
        }
        at = count * 2u;
        for (i = 0; i < count; ++i) {
            offsets[i] = (unsigned)at;
            while (used < strings.count && strings.data[used]) ++used;
            ++used;
            at = count * 2u + used;
        }
        for (i = 0; i < count; ++i) if (fe2_buf_u16(&blob, offsets[i])) { fe2_buf_free(&strings); goto done; }
        if (fe2_buf_add(&blob, strings.data, strings.count)) { fe2_buf_free(&strings); goto done; }
        fe2_buf_free(&strings);
        if (directory + (long long)blob.count != end_offset) goto done;
        status = place_at(image, SYSTEMS_BASE, SYSTEMS_END, spans, span_n, (unsigned)directory, blob.data, blob.count);
    }
done:
    fe2_buf_free(&blob);
    fe2_ini_free(&ini);
    return status;
}

static int compile_handcrafted_keys(const char *root, uint8_t *image, fe2_span *spans, size_t *span_n)
{
    fe2_ini keys, templates;
    fe2_ini_section *section;
    fe2_csv csv;
    char path[4096], templates_path[4096];
    const char *entries, *templates_rel, *start_text, *end_text;
    long long start, end;
    fe2_buf blob;
    size_t s, row;
    int status = -1;
    memset(&keys, 0, sizeof keys);
    memset(&templates, 0, sizeof templates);
    memset(&csv, 0, sizeof csv);
    memset(&blob, 0, sizeof blob);
    if (under(root, "systems/handcrafted/handcrafted_systems.ini", path, sizeof path) || fe2_ini_load(&keys, path))
        goto done;
    section = fe2_ini_section_find(&keys, "handcrafted_systems");
    if (!section) goto done;
    entries = fe2_ini_get(section, "entries");
    templates_rel = fe2_ini_get(section, "templates");
    start_text = fe2_ini_get(section, "start");
    end_text = fe2_ini_get(section, "end");
    if (!entries || !templates_rel || !start_text || !end_text || parse_i64(start_text, &start) || parse_i64(end_text, &end))
        goto done;
    {
        char base[4096];
        const char *slash = strrchr(path, '/');
        const char *back = strrchr(path, '\\');
        const char *cut = !slash ? back : !back ? slash : (slash > back ? slash : back);
        size_t n = cut ? (size_t)(cut - path) : 0;
        if (!n || n >= sizeof base) goto done;
        memcpy(base, path, n);
        base[n] = 0;
        if (fe2_path_join(templates_path, sizeof templates_path, base, templates_rel) || fe2_ini_load(&templates, templates_path))
            goto done;
        if (fe2_path_join(path, sizeof path, base, entries) || csv_load(path, &csv)) goto done;
    }
    if (csv.n < 2 || csv.rows[0].n != 4 || strcmp(csv.rows[0].cell[0], "sector_x") ||
        strcmp(csv.rows[0].cell[1], "sector_y") || strcmp(csv.rows[0].cell[2], "number") ||
        strcmp(csv.rows[0].cell[3], "template")) goto done;
    for (row = 1; row < csv.n; ++row) {
        long long sx, sy, number, map_x, map_y;
        unsigned offset = 0;
        int found = 0;
        uint32_t key;
        uint8_t bytes[6];
        char section_name[64];
        if (csv.rows[row].n != 4 || parse_dec(csv.rows[row].cell[0], &sx) || parse_dec(csv.rows[row].cell[1], &sy) ||
            parse_dec(csv.rows[row].cell[2], &number) || !strcmp(csv.rows[row].cell[3], "fallback")) goto done;
        map_x = sx + CENTRE_X;
        map_y = sy + CENTRE_Y;
        if (map_x < 0 || map_x >= 8192 || map_y < 0 || map_y >= 8192 || number < 0 || number >= 64) goto done;
        key = ((uint32_t)map_y << 19) | ((uint32_t)map_x << 6) | (uint32_t)number;
        if (!key) goto done;
        if (snprintf(section_name, sizeof section_name, "system_template_%s", csv.rows[row].cell[3]) >= (int)sizeof section_name)
            goto done;
        for (s = 0; s < templates.count; ++s) if (!strcmp(templates.sections[s].name, section_name)) {
            const char *at = fe2_ini_get(&templates.sections[s], "start");
            long long value;
            if (!at || parse_i64(at, &value) || value < 0 || value > 0xffff) goto done;
            offset = (unsigned)value;
            found = 1;
        }
        if (!found) goto done;
        for (s = 0; s + 6 <= blob.count; s += 6) {
            uint32_t previous = (uint32_t)blob.data[s] | ((uint32_t)blob.data[s + 1] << 8) |
                ((uint32_t)blob.data[s + 2] << 16) | ((uint32_t)blob.data[s + 3] << 24);
            if (previous == key) goto done;
        }
        bytes[0] = (uint8_t)key; bytes[1] = (uint8_t)(key >> 8); bytes[2] = (uint8_t)(key >> 16); bytes[3] = (uint8_t)(key >> 24);
        bytes[4] = (uint8_t)offset; bytes[5] = (uint8_t)(offset >> 8);
        if (fe2_buf_add(&blob, bytes, 6)) goto done;
    }
    if (start + (long long)blob.count + 4 != end || fe2_buf_add(&blob, "\0\0\0\0\xcb\xa3", 6)) goto done;
    status = place_at(image, SYSTEMS_BASE, SYSTEMS_END, spans, span_n, (unsigned)start, blob.data, blob.count);
done:
    csv_free(&csv);
    fe2_buf_free(&blob);
    fe2_ini_free(&keys);
    fe2_ini_free(&templates);
    return status;
}

static int compile_systems_ini(const fe2_names *names, const char *path, const char *dir, uint8_t *image,
                               fe2_span *spans, size_t *span_n)
{
    fe2_ini ini;
    size_t section;
    int status = -1;
    memset(&ini, 0, sizeof ini);
    if (fe2_ini_load(&ini, path)) goto done;
    for (section = 0; section < ini.count; ++section) {
        const fe2_ini_section *item = &ini.sections[section];
        const char *kind = fe2_ini_get(item, "type");
        const char *file = fe2_ini_get(item, "file");
        const char *start_text = fe2_ini_get(item, "start");
        const char *end_text = fe2_ini_get(item, "end");
        long long start, end;
        char file_path[4096];
        fe2_csv csv;
        fe2_buf blob;
        memset(&csv, 0, sizeof csv);
        memset(&blob, 0, sizeof blob);
        if (!kind || !file || !start_text || !end_text || parse_i64(start_text, &start) || parse_i64(end_text, &end))
            goto section_done;
        if (snprintf(file_path, sizeof file_path, "%s/%s", dir, file) >= (int)sizeof file_path) goto section_done;
        if (!strcmp(kind, "object_record")) {
            if (compile_object(names, file_path, &blob)) goto section_done;
        } else if (csv_load(file_path, &csv)) goto section_done;
        else if (!strcmp(kind, "u8") || !strcmp(kind, "u16") || !strcmp(kind, "u32")) {
            long long *values = NULL;
            size_t n = 0;
            unsigned width = kind[1] == '8' ? 1u : kind[1] == '1' ? 2u : 4u;
            if (csv_integers(&csv, &values, &n) || pack_words(&blob, values, n, width)) { free(values); goto section_done; }
            free(values);
        } else if (!strcmp(kind, "model_id")) {
            if (compile_model_ids(names, &csv, &blob)) goto section_done;
        } else if (!strcmp(kind, "record")) {
            if (compile_record(names, item, &csv, &blob)) goto section_done;
        } else if (!strcmp(kind, "system_template")) {
            if (compile_template(names, &csv, &blob)) goto section_done;
        } else goto section_done;
        if (blob.count != (size_t)(end - start) ||
            place_at(image, SYSTEMS_BASE, SYSTEMS_END, spans, span_n, (unsigned)start, blob.data, blob.count))
            goto section_done;
        csv_free(&csv);
        fe2_buf_free(&blob);
        continue;
    section_done:
        csv_free(&csv);
        fe2_buf_free(&blob);
        goto done;
    }
    status = 0;
done:
    fe2_ini_free(&ini);
    return status;
}

int fe2_compile_systems_image(const char *data_root, uint8_t **out, size_t *out_n)
{
    uint8_t *image = NULL;
    fe2_names *names = NULL;
    fe2_span spans[64];
    size_t span_n = 0;
    uint8_t marker = 0xff;
    char path[4096];
    int status = -1;
    if (!data_root || !out || !out_n) return -1;
    *out = NULL;
    *out_n = 0;
    image = (uint8_t *)calloc(SYSTEMS_BYTES, 1);
    names = (fe2_names *)calloc(1, sizeof *names);
    if (!image || !names) goto done;
    if (load_models(names, data_root) || place_at(image, SYSTEMS_BASE, SYSTEMS_END, spans, &span_n, SYSTEMS_BASE, &marker, 1))
        goto done;
    if (snprintf(path, sizeof path, "%s/profiles/profiles.csv", data_root) >= (int)sizeof path ||
        compile_profiles(names, path, image, spans, &span_n) ||
        compile_system_strings(data_root, image, spans, &span_n) ||
        compile_handcrafted_keys(data_root, image, spans, &span_n)) goto done;
    {
        char dir[4096], ini[4096];
        if (snprintf(dir, sizeof dir, "%s/profiles", data_root) >= (int)sizeof dir ||
            snprintf(ini, sizeof ini, "%s/tables.ini", dir) >= (int)sizeof ini ||
            compile_systems_ini(names, ini, dir, image, spans, &span_n)) goto done;
        if (snprintf(dir, sizeof dir, "%s/systems/handcrafted", data_root) >= (int)sizeof dir ||
            snprintf(ini, sizeof ini, "%s/tables.ini", dir) >= (int)sizeof ini ||
            compile_systems_ini(names, ini, dir, image, spans, &span_n)) goto done;
    }
    if (systems_covered(spans, span_n)) goto done;
    *out = image;
    *out_n = SYSTEMS_BYTES;
    image = NULL;
    status = 0;
done:
    free(image);
    names_free(names);
    free(names);
    if (status) fe2_asset_fail("systems resources must cover 00A2h..3F20h exactly");
    return status;
}

static const char *government_labels[] = {
    "None", "None (Anarchy)", "Rule by force of local Barons",
    "Policed by the local corporations", "Corporate State", "Federal Democracy",
    "Federal Colonial Rule", "Dictatorship", "Communist", "Religious Dictatorship",
    "Independent Democracy", "Imperial Rule", "Imperial Colonial Rule",
    "No Stable Government", "Martial Law."
};

static int choice_label(const char *token, const char *const *labels, size_t count, unsigned *out)
{
    size_t i;
    for (i = 0; i < count; ++i) if (!strcmp(token, labels[i])) { *out = (unsigned)i; return 0; }
    return -1;
}

static int trade_code(const char *token, unsigned *out)
{
    unsigned illegal = 0;
    if (token[0] == '0' && token[1] == 'x' && token[2] && token[3] && !token[4]) {
        unsigned value;
        if (sscanf(token, "%x", &value) != 1 || value > 255) return -1;
        *out = value;
        return 0;
    }
    if (token[0] == '!') { illegal = 1; ++token; }
    if ((token[0] == 'i' || token[0] == 'e') && token[1] >= '0' && token[1] <= '7' && !token[2]) {
        *out = (illegal ? 128u : 0u) | (token[0] == 'e' ? 8u : 0u) | (unsigned)(token[1] - '0');
        return 0;
    }
    return -1;
}

static int text_selector(const fe2_names *names, const char *token, const char *catalogue, unsigned *out)
{
    size_t i, c;
    long long value;
    if (strchr(token, ':')) {
        if (strncmp(token, catalogue, strlen(catalogue)) || token[strlen(catalogue)] != ':') return -1;
        return resolve_text(names, token, out);
    }
    if (parse_i64(token, &value) || value < 0 || value > 65535) return -1;
    for (c = 0; c < 5; ++c) for (i = 0; i < names->text[c].count; ++i)
        if (names->text[c].base + (unsigned)i == (unsigned)value) return -1;
    *out = (unsigned)value;
    return 0;
}

int fe2_compile_catalogue(const char *data_root, uint8_t **out, size_t *out_n)
{
    static const char *prefix[] = {
        "record", "system", "sector_x", "sector_y", "number", "population",
        "resource_richness", "category_0_rate", "market_profile", "category_1_rate", "market_counter_seed",
        "government", "allegiance", "economic_factor_index"
    };
    fe2_names *names = NULL;
    fe2_csv csv;
    char commodities[31][64];
    char path[4096];
    uint8_t *image = NULL;
    size_t row, i;
    int coord_x[47], coord_y[47];
    unsigned bounds[50];
    int status = -1;
    memset(&csv, 0, sizeof csv);
    memset(commodities, 0, sizeof commodities);
    if (!data_root || !out || !out_n) return -1;
    *out = NULL;
    *out_n = 0;
    names = (fe2_names *)calloc(1, sizeof *names);
    if (!names || load_texts(names, data_root) || chart_names(names, data_root) ||
        catalog_layout(data_root, coord_x, coord_y, bounds)) goto done;
    if (snprintf(path, sizeof path, "%s/text/market/strings.tsv", data_root) >= (int)sizeof path) goto done;
    {
        char **lines = NULL;
        size_t count = 0;
        if (load_lines(path, &lines, &count, 0) || count != 31) { free_lines(lines, count); goto done; }
        for (i = 0; i < 31; ++i) {
            char *tab = strchr(lines[i], '\t');
            if (tab) *tab = 0;
            if (strlen(lines[i]) >= 64) { free_lines(lines, count); goto done; }
            memcpy(commodities[i], lines[i], strlen(lines[i]) + 1);
        }
        free_lines(lines, count);
    }
    if (snprintf(path, sizeof path, "%s/systems/catalogue/records.tsv", data_root) >= (int)sizeof path ||
        csv_load_delim(path, &csv, '\t') || csv.n != 332 || csv.rows[0].n != 49) goto done;
    for (i = 0; i < 14; ++i) if (strcmp(csv.rows[0].cell[i], prefix[i])) goto done;
    for (i = 0; i < 31; ++i) if (strcmp(csv.rows[0].cell[14 + i], commodities[i])) goto done;
    if (strcmp(csv.rows[0].cell[45], "low_strength") || strcmp(csv.rows[0].cell[46], "high_strength") ||
        strcmp(csv.rows[0].cell[47], "map_description_text") || strcmp(csv.rows[0].cell[48], "system_view_text"))
        goto done;
    image = (uint8_t *)calloc(331u * 48u, 1);
    if (!image) goto done;
    for (row = 0; row < 331; ++row) {
        const fe2_csv_row *item = &csv.rows[row + 1];
        uint8_t *rec = image + row * 48u;
        char expected[64], label[32];
        long long sx, sy, number, byte;
        unsigned population, government, allegiance, map_text, view_text, low, high;
        long long map_x, map_y;
        uint32_t key;
        if (item->n != 49) goto done;
        if (row < 7) snprintf(label, sizeof label, "fallback_record_%u", (unsigned)row);
        else snprintf(label, sizeof label, "keyed_record_%03u", (unsigned)(row - 7));
        if (strcmp(item->cell[0], label) || parse_dec(item->cell[2], &sx) || parse_dec(item->cell[3], &sy) ||
            parse_dec(item->cell[4], &number)) goto done;
        if (map_name(names, data_root, (int)sx, (int)sy, (int)number, coord_x, coord_y, bounds, expected, sizeof expected) ||
            strcmp(item->cell[1], expected)) goto done;
        map_x = sx + CENTRE_X;
        map_y = sy + CENTRE_Y;
        key = ((uint32_t)map_y << 19) | ((uint32_t)map_x << 6) | (uint32_t)number;
        if (!key) goto done;
        if (choice_label(item->cell[5], population_labels, 10, &population) ||
            choice_label(item->cell[11], government_labels, 15, &government) ||
            allegiance_label(names, data_root, 0, expected, sizeof expected)) goto done;
        {
            char labels[3][64];
            unsigned a;
            for (a = 0; a < 3; ++a) if (allegiance_label(names, data_root, a, labels[a], sizeof labels[a])) goto done;
            allegiance = 3;
            for (a = 0; a < 3; ++a) if (!strcmp(item->cell[12], labels[a])) allegiance = a;
            if (allegiance == 3) goto done;
        }
        rec[4] = (uint8_t)population;
        for (i = 0; i < 5; ++i) {
            if (parse_dec(item->cell[6 + i], &byte) || byte < 0 || byte > 255) goto done;
            rec[5 + i] = (uint8_t)byte;
        }
        rec[10] = (uint8_t)(government | (allegiance << 6));
        if (parse_dec(item->cell[13], &byte) || byte < 0 || byte > 255) goto done;
        rec[11] = (uint8_t)byte;
        for (i = 0; i < 31; ++i) {
            unsigned code;
            if (trade_code(item->cell[14 + i], &code)) goto done;
            rec[12 + i] = (uint8_t)code;
        }
        if (parse_dec(item->cell[45], &byte) || byte < 0 || byte > 15) goto done;
        low = (unsigned)byte;
        if (parse_dec(item->cell[46], &byte) || byte < 0 || byte > 15) goto done;
        high = (unsigned)byte;
        rec[43] = (uint8_t)(low | (high << 4));
        if (text_selector(names, item->cell[47], "chart", &map_text) ||
            text_selector(names, item->cell[48], "systems", &view_text)) goto done;
        rec[0] = (uint8_t)key; rec[1] = (uint8_t)(key >> 8); rec[2] = (uint8_t)(key >> 16); rec[3] = (uint8_t)(key >> 24);
        rec[44] = (uint8_t)map_text; rec[45] = (uint8_t)(map_text >> 8);
        rec[46] = (uint8_t)view_text; rec[47] = (uint8_t)(view_text >> 8);
        (void)expected;
    }
    *out = image;
    *out_n = 331u * 48u;
    image = NULL;
    status = 0;
done:
    free(image);
    csv_free(&csv);
    names_free(names);
    free(names);
    if (status) fe2_asset_fail("catalogue record count changed");
    return status;
}

static int parse_c_int(const char *text, unsigned long *out, int allow_hex)
{
    char *end = NULL;
    int base = 10;
    if (allow_hex && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) base = 16;
    *out = strtoul(text, &end, base);
    if (end == text || *end || (*out == 0xfffffffful && text[0] != '0')) return -1;
    return 0;
}

static int csv_words(const char *path, unsigned long *out, unsigned expect, int allow_hex)
{
    fe2_csv csv;
    unsigned n = 0;
    size_t row, col;
    memset(&csv, 0, sizeof csv);
    if (csv_load(path, &csv)) return -1;
    for (row = 0; row < csv.n; ++row) {
        for (col = 0; col < csv.rows[row].n; ++col) {
            if (!csv.rows[row].cell[col][0]) continue;
            if (n >= expect || parse_c_int(csv.rows[row].cell[col], &out[n], allow_hex)) {
                csv_free(&csv);
                return -1;
            }
            ++n;
        }
    }
    csv_free(&csv);
    return n == expect ? 0 : -1;
}

static int emit_bytes(uint8_t **out, size_t *out_n, const uint8_t *data, size_t count)
{
    uint8_t *copy = (uint8_t *)malloc(count);
    if (!copy) return -1;
    memcpy(copy, data, count);
    *out = copy;
    *out_n = count;
    return 0;
}

int fe2_compile_engine_table(const char *data_root, const char *name, uint8_t **out, size_t *out_n)
{
    char path[512];
    unsigned long words[256];
    uint8_t bytes[256];
    unsigned i;
    if (!data_root || !name || !out || !out_n) return -1;
    *out = NULL;
    *out_n = 0;
    if (strcmp(name, "input") == 0) {
        unsigned long shifted[128], plain[128];
        if (snprintf(path, sizeof path, "%s/main/frontier/main_keyboard_shifted.csv", data_root) >= (int)sizeof path ||
            csv_words(path, shifted, 128, 1)) return -1;
        if (snprintf(path, sizeof path, "%s/main/frontier/main_keyboard_unshifted.csv", data_root) >= (int)sizeof path ||
            csv_words(path, plain, 128, 1)) return -1;
        for (i = 0; i < 128; ++i) {
            if (shifted[i] > 255 || plain[i] > 255) return -1;
            bytes[i] = (uint8_t)shifted[i];
            bytes[128 + i] = (uint8_t)plain[i];
        }
        return emit_bytes(out, out_n, bytes, 256);
    }
    if (strcmp(name, "pointer") == 0) {
        static const char *files[2] = {"main_function_key_regions.csv", "main_missile_button_regions.csv"};
        static const unsigned counts[2] = {16u, 5u};
        uint8_t image[210];
        unsigned at = 0;
        unsigned part;
        for (part = 0; part < 2; ++part) {
            fe2_csv csv;
            size_t row;
            memset(&csv, 0, sizeof csv);
            if (snprintf(path, sizeof path, "%s/main/frontier/%s", data_root, files[part]) >= (int)sizeof path ||
                csv_load(path, &csv) || csv.n != counts[part]) {
                csv_free(&csv);
                return -1;
            }
            for (row = 0; row < csv.n; ++row) {
                unsigned col;
                if (csv.rows[row].n != 5) { csv_free(&csv); return -1; }
                for (col = 0; col < 5; ++col) {
                    unsigned long value;
                    if (parse_c_int(csv.rows[row].cell[col], &value, 0) || value > 65535ul) {
                        csv_free(&csv);
                        return -1;
                    }
                    image[at++] = (uint8_t)value;
                    image[at++] = (uint8_t)(value >> 8);
                }
            }
            csv_free(&csv);
        }
        return emit_bytes(out, out_n, image, 210);
    }
    if (strcmp(name, "pool") == 0) {
        if (snprintf(path, sizeof path, "%s/main/frontier/main_object_adjustment_words.csv", data_root) >= (int)sizeof path ||
            csv_words(path, words, 15, 0)) return -1;
        for (i = 0; i < 15; ++i) {
            if (words[i] > 65535ul) return -1;
            bytes[i * 2] = (uint8_t)words[i];
            bytes[i * 2 + 1] = (uint8_t)(words[i] >> 8);
        }
        return emit_bytes(out, out_n, bytes, 30);
    }
    if (strcmp(name, "motion") == 0) {
        if (snprintf(path, sizeof path, "%s/main/frontier/basis_rotation_rebuild_costs.csv", data_root) >= (int)sizeof path ||
            csv_words(path, words, 10, 0)) return -1;
        for (i = 0; i < 10; ++i) {
            if (words[i] > 255ul) return -1;
            bytes[i] = (uint8_t)words[i];
        }
        return emit_bytes(out, out_n, bytes, 10);
    }
    if (strcmp(name, "contact") == 0 || strcmp(name, "probe") == 0) {
        fe2_csv csv;
        size_t row;
        unsigned at = 0;
        memset(&csv, 0, sizeof csv);
        if (snprintf(path, sizeof path, "%s/main/frontier/contact_axis_permutations.csv", data_root) >= (int)sizeof path ||
            csv_load(path, &csv) || csv.n != 6) {
            csv_free(&csv);
            return -1;
        }
        for (row = 0; row < 6; ++row) {
            unsigned col;
            if (csv.rows[row].n < 3) { csv_free(&csv); return -1; }
            for (col = 0; col < 3; ++col) {
                unsigned long value;
                if (parse_c_int(csv.rows[row].cell[col], &value, 0) || value > 510ul || (value & 1ul)) {
                    csv_free(&csv);
                    return -1;
                }
                bytes[at++] = (uint8_t)(value / 2ul);
            }
        }
        csv_free(&csv);
        return emit_bytes(out, out_n, bytes, 18);
    }
    if (strcmp(name, "position") == 0) {
        static const uint32_t past[8] = {
            0x200b0e80u, 0xfbeae802u, 0x0072bb50u, 0x07ec36c4u,
            0x0800f626u, 0x794b0775u, 0x02c483f7u, 0x065653cbu
        };
        fe2_csv csv;
        uint8_t image[128];
        size_t row;
        unsigned at = 0;
        memset(&csv, 0, sizeof csv);
        if (snprintf(path, sizeof path, "%s/main/frontier/object_variant_parameter_sets.csv", data_root) >= (int)sizeof path ||
            csv_load(path, &csv) || csv.n != 6) {
            csv_free(&csv);
            return -1;
        }
        for (row = 0; row < 6; ++row) {
            unsigned col;
            if (csv.rows[row].n != 4) { csv_free(&csv); return -1; }
            for (col = 0; col < 4; ++col) {
                unsigned long value;
                if (parse_c_int(csv.rows[row].cell[col], &value, 1)) { csv_free(&csv); return -1; }
                image[at++] = (uint8_t)value;
                image[at++] = (uint8_t)(value >> 8);
                image[at++] = (uint8_t)(value >> 16);
                image[at++] = (uint8_t)(value >> 24);
            }
        }
        csv_free(&csv);
        for (i = 0; i < 8; ++i) {
            image[at++] = (uint8_t)past[i];
            image[at++] = (uint8_t)(past[i] >> 8);
            image[at++] = (uint8_t)(past[i] >> 16);
            image[at++] = (uint8_t)(past[i] >> 24);
        }
        return emit_bytes(out, out_n, image, 128);
    }
    if (strcmp(name, "objects") == 0) {
        static const char *word_files[] = {
            "main_object_adjustment_words.csv", "main_equipment_text_selectors.csv"
        };
        static const unsigned word_counts[] = {15u, 0u};
        static const char *byte_files[] = {
            "main_equipment_deployment_map.csv", "main_class_deployment_map.csv"
        };
        uint8_t image[0x9b];
        unsigned at = 0;
        unsigned part;
        unsigned long count;
        /* Word counts are filled after reading; the selectors length is not fixed here. */
        (void)word_counts;
        for (part = 0; part < 2; ++part) {
            fe2_csv csv;
            size_t row, col;
            memset(&csv, 0, sizeof csv);
            if (snprintf(path, sizeof path, "%s/main/frontier/%s", data_root, word_files[part]) >= (int)sizeof path ||
                csv_load(path, &csv)) return -1;
            for (row = 0; row < csv.n; ++row) {
                for (col = 0; col < csv.rows[row].n; ++col) {
                    if (!csv.rows[row].cell[col][0]) continue;
                    if (parse_c_int(csv.rows[row].cell[col], &count, 0) || count > 65535ul || at + 2 > 0x8f) {
                        csv_free(&csv);
                        return -1;
                    }
                    image[at++] = (uint8_t)count;
                    image[at++] = (uint8_t)(count >> 8);
                }
            }
            csv_free(&csv);
            if (part == 0 && at != 30) return -1;
        }
        image[at++] = 0xff;
        image[at++] = 0xff;
        for (part = 0; part < 2; ++part) {
            fe2_csv csv;
            size_t row, col;
            memset(&csv, 0, sizeof csv);
            if (snprintf(path, sizeof path, "%s/main/frontier/%s", data_root, byte_files[part]) >= (int)sizeof path ||
                csv_load(path, &csv)) return -1;
            for (row = 0; row < csv.n; ++row) {
                for (col = 0; col < csv.rows[row].n; ++col) {
                    if (!csv.rows[row].cell[col][0]) continue;
                    if (parse_c_int(csv.rows[row].cell[col], &count, 0) || count > 255ul || at >= 0x8f) {
                        csv_free(&csv);
                        return -1;
                    }
                    image[at++] = (uint8_t)count;
                }
            }
            csv_free(&csv);
        }
        for (i = 0; i < 10; ++i) {
            if (at >= 0x8f) return -1;
            image[at++] = 0;
        }
        {
            static const char *tail_files[] = {"main_effect_shift_tail.csv", "main_effect_strength_words.csv"};
            static const unsigned char code[] = {0xfa, 0xb8, 0x54, 0x31, 0x8e, 0xd0, 0xbc, 0x04, 0xfa, 0xfb, 0xe8, 0x55};
            for (part = 0; part < 2; ++part) {
                fe2_csv csv;
                size_t row, col;
                memset(&csv, 0, sizeof csv);
                if (snprintf(path, sizeof path, "%s/main/frontier/%s", data_root, tail_files[part]) >= (int)sizeof path ||
                    csv_load(path, &csv)) return -1;
                for (row = 0; row < csv.n; ++row) {
                    for (col = 0; col < csv.rows[row].n; ++col) {
                        if (!csv.rows[row].cell[col][0]) continue;
                        if (parse_c_int(csv.rows[row].cell[col], &count, 0) || count > 65535ul || at + 2 > 0x8f) {
                            csv_free(&csv);
                            return -1;
                        }
                        image[at++] = (uint8_t)count;
                        image[at++] = (uint8_t)(count >> 8);
                    }
                }
                csv_free(&csv);
            }
            if (at != 0x8f) return -1;
            memcpy(image + at, code, sizeof code);
        }
        return emit_bytes(out, out_n, image, 0x9b);
    }
    return -1;
}

static int place_words(const char *path, uint8_t *image, unsigned at, unsigned count, unsigned width)
{
    unsigned long values[256];
    unsigned i, byte;
    if (count > 256 || csv_words(path, values, count, 1)) return -1;
    for (i = 0; i < count; ++i) {
        if (width == 1 && values[i] > 255ul) return -1;
        if (width == 2 && values[i] > 65535ul) return -1;
        for (byte = 0; byte < width; ++byte)
            image[at + i * width + byte] = (uint8_t)(values[i] >> (8u * byte));
    }
    return 0;
}

int fe2_compile_people(const char *data_root, uint8_t **out, size_t *out_n)
{
    fe2_names names;
    fe2_buf body;
    char path[512];
    uint8_t image[493];
    unsigned long words[26];
    unsigned i;
    int status = -1;
    if (!data_root || !out || !out_n) return -1;
    memset(&names, 0, sizeof names);
    memset(&body, 0, sizeof body);
    *out = NULL;
    if (load_models(&names, data_root)) goto done;
    if (under(data_root, "objects/object_template.txt", path, sizeof path) ||
        compile_object(&names, path, &body) || body.count != 0x11e) goto done;
    memcpy(image, body.data, 0x11e);
    if (under(data_root, "objects/object_parameter_tables.csv", path, sizeof path) ||
        place_words(path, image, 0x11e, 155, 1)) goto done;
    if (under(data_root, "objects/variant_object_values.csv", path, sizeof path) ||
        csv_words(path, words, 26, 1)) goto done;
    for (i = 0; i < 26; ++i) {
        if (words[i] > 65535ul) goto done;
        image[0x11e + 155 + i * 2] = (uint8_t)words[i];
        image[0x11e + 155 + i * 2 + 1] = (uint8_t)(words[i] >> 8);
    }
    status = emit_bytes(out, out_n, image, 493);
done:
    fe2_buf_free(&body);
    names_free(&names);
    return status;
}

int fe2_compile_panels(const char *data_root, uint8_t **out, size_t *out_n)
{
    char path[512];
    uint8_t image[93];
    if (!data_root || !out || !out_n) return -1;
    memset(image, 0, sizeof image);
    if (under(data_root, "panels/overlay_event_mask.csv", path, sizeof path) ||
        place_words(path, image, 0, 1, 4) ||
        under(data_root, "panels/palette_mapping.csv", path, sizeof path) ||
        place_words(path, image, 4, 22, 1) ||
        under(data_root, "panels/palette_list_end.csv", path, sizeof path) ||
        place_words(path, image, 26, 1, 1) || image[26] != 0xff ||
        under(data_root, "panels/marker_colour_pointers.csv", path, sizeof path) ||
        place_words(path, image, 27, 18, 2) ||
        under(data_root, "panels/drive_gauge_scales.csv", path, sizeof path) ||
        place_words(path, image, 63, 15, 2)) return -1;
    return emit_bytes(out, out_n, image, 93);
}

int fe2_compile_intro_tables(const char *data_root, uint8_t **out, size_t *out_n)
{
    fe2_ini ini;
    fe2_csv csv;
    char path[512];
    uint8_t image[294];
    const char *scenes, *length_text, *module;
    unsigned long orbit_length, previous_end = 0;
    size_t row;
    unsigned at = 0;
    int status = -1;
    memset(&ini, 0, sizeof ini);
    memset(&csv, 0, sizeof csv);
    memset(image, 0, sizeof image);
    if (!data_root || !out || !out_n) return -1;
    if (under(data_root, "intro/timeline.ini", path, sizeof path) || fe2_ini_load(&ini, path)) goto done;
    {
        fe2_ini_section *section = fe2_ini_section_find(&ini, "timeline");
        module = section ? fe2_ini_get(section, "module") : NULL;
        scenes = section ? fe2_ini_get(section, "scenes") : NULL;
        length_text = section ? fe2_ini_get(section, "orbit_length") : NULL;
        if (!module || strcmp(module, "intro") != 0 || !scenes || !length_text ||
            parse_c_int(length_text, &orbit_length, 1) || orbit_length == 0) goto done;
    }
    if (under(data_root, "intro/timeline.ini", path, sizeof path)) goto done;
    {
        char *slash = strrchr(path, '/');
        if (!slash) goto done;
        if ((size_t)(slash - path) + 1 + strlen(scenes) >= sizeof path) goto done;
        memcpy(slash + 1, scenes, strlen(scenes) + 1);
    }
    if (csv_load(path, &csv) || csv.n != 23 || csv.rows[0].n < 5) goto done;
    if (strcmp(csv_cell(&csv, 0, 0), "scene") != 0 || strcmp(csv_cell(&csv, 0, 1), "start") != 0 ||
        strcmp(csv_cell(&csv, 0, 2), "end") != 0 || strcmp(csv_cell(&csv, 0, 3), "divisor") != 0 ||
        strcmp(csv_cell(&csv, 0, 4), "phase_bias") != 0) goto done;
    for (row = 1; row < csv.n; ++row) {
        unsigned long index, start, end, divisor, bias, span;
        unsigned byte;
        if (csv.rows[row].n != 5) goto done;
        if (parse_c_int(csv.rows[row].cell[0], &index, 1) || index != row - 1 ||
            parse_c_int(csv.rows[row].cell[1], &start, 1) ||
            parse_c_int(csv.rows[row].cell[2], &end, 1) ||
            parse_c_int(csv.rows[row].cell[3], &divisor, 1) ||
            parse_c_int(csv.rows[row].cell[4], &bias, 1)) goto done;
        if (start >= end || divisor == 0 || divisor > 65535ul || bias > 65535ul) goto done;
        span = end - start;
        if (span > (1ul << 28) || ((span - 1ul) << 4) / divisor > 65535ul) goto done;
        if (row == 1) {
            if (end > 0xfffffffful - orbit_length) goto done;
            previous_end = end;
        }
        for (byte = 0; byte < 4; ++byte) image[at++] = (uint8_t)(start >> (8 * byte));
        for (byte = 0; byte < 4; ++byte) image[at++] = (uint8_t)(end >> (8 * byte));
        image[at++] = (uint8_t)divisor;
        image[at++] = (uint8_t)(divisor >> 8);
        image[at++] = (uint8_t)bias;
        image[at++] = (uint8_t)(bias >> 8);
    }
    for (row = 0; row < 4; ++row) image[at++] = (uint8_t)(previous_end >> (8 * row));
    for (row = 0; row < 4; ++row) image[at++] = (uint8_t)(orbit_length >> (8 * row));
    if (under(data_root, "intro/initial_render_vector.csv", path, sizeof path) ||
        place_words(path, image, at, 3, 2)) goto done;
    at += 6;
    if (under(data_root, "intro/initial_classification_values.csv", path, sizeof path) ||
        place_words(path, image, at, 4, 4)) goto done;
    status = emit_bytes(out, out_n, image, 294);
done:
    csv_free(&csv);
    fe2_ini_free(&ini);
    return status;
}

static int escaped_span(const char *path, fe2_buf *out, size_t expect)
{
    char *text;
    size_t n, before;
    text = fe2_read_text(path, NULL);
    if (!text) return -1;
    n = strlen(text);
    {
        char *src = text, *dst = text;
        while (*src) {
            if (*src != '\r') *dst++ = *src;
            ++src;
        }
        *dst = 0;
        n = (size_t)(dst - text);
    }
    if (n && text[n - 1] == '\n') text[n - 1] = 0;
    before = out->count;
    if (decode_text(text, out)) { free(text); return -1; }
    free(text);
    return out->count - before == expect ? 0 : -1;
}

int fe2_compile_navigation(const char *data_root, uint8_t **out, size_t *out_n)
{
    fe2_buf text;
    char path[512];
    uint8_t image[0x6a];
    unsigned at;
    memset(&text, 0, sizeof text);
    if (!data_root || !out || !out_n) return -1;
    image[0] = 0xff;
    image[1] = 4;
    image[2] = 0;
    image[3] = 0x18;
    image[4] = 0;
    at = 5;
    if (under(data_root, "navigation/time_point_format.txt", path, sizeof path) ||
        escaped_span(path, &text, 15) ||
        under(data_root, "navigation/target_prompt_stream.txt", path, sizeof path) ||
        escaped_span(path, &text, 4)) goto fail;
    if (fe2_buf_byte(&text, 0) ||
        under(data_root, "navigation/target_prompt_text.txt", path, sizeof path) ||
        escaped_span(path, &text, 46)) goto fail;
    if (text.count != 15 + 4 + 1 + 46) goto fail;
    memcpy(image + at, text.data, text.count);
    at += (unsigned)text.count;
    if (under(data_root, "navigation/drawing_parameters.csv", path, sizeof path) ||
        place_words(path, image, at, 7, 2)) goto fail;
    at += 14;
    if (under(data_root, "navigation/marker_shape.csv", path, sizeof path) ||
        place_words(path, image, at, 20, 1)) goto fail;
    at += 20;
    image[at++] = 0xcb;
    if (at != 0x6a) goto fail;
    fe2_buf_free(&text);
    return emit_bytes(out, out_n, image, 0x6a);
fail:
    fe2_buf_free(&text);
    return -1;
}

static int append_units(const char *path, fe2_buf *out, unsigned width, int skip_header, unsigned expect)
{
    fe2_csv csv;
    size_t row, col, start = 0, before;
    memset(&csv, 0, sizeof csv);
    if (csv_load(path, &csv)) return -1;
    if (skip_header && csv.n) {
        int textual = 0;
        for (col = 0; col < csv.rows[0].n; ++col) {
            unsigned long value;
            if (csv.rows[0].cell[col][0] && parse_c_int(csv.rows[0].cell[col], &value, 1)) textual = 1;
        }
        if (textual) start = 1;
    }
    before = out->count;
    for (row = start; row < csv.n; ++row) {
        for (col = 0; col < csv.rows[row].n; ++col) {
            unsigned long value;
            unsigned byte;
            if (!csv.rows[row].cell[col][0]) continue;
            if (parse_c_int(csv.rows[row].cell[col], &value, 1) ||
                (width == 1 && value > 255ul) || (width == 2 && value > 65535ul)) {
                csv_free(&csv);
                return -1;
            }
            for (byte = 0; byte < width; ++byte)
                if (fe2_buf_byte(out, (uint8_t)(value >> (8u * byte)))) { csv_free(&csv); return -1; }
        }
    }
    csv_free(&csv);
    return expect == 0 || out->count - before == expect ? 0 : -1;
}

static int append_u32(fe2_buf *out, uint32_t value)
{
    unsigned byte;
    for (byte = 0; byte < 4; ++byte)
        if (fe2_buf_byte(out, (uint8_t)(value >> (8u * byte)))) return -1;
    return 0;
}

int fe2_compile_render(const char *data_root, uint8_t **out, size_t *out_n)
{
    static const char *fields[] = {
        "index", "graphics_selector", "color0", "color1", "color2", "color3", "color4", "color5", "color6"
    };
    fe2_csv csv;
    fe2_buf record, tail, material, rest;
    char path[512];
    size_t row;
    unsigned i;
    int status = -1;
    memset(&csv, 0, sizeof csv);
    memset(&record, 0, sizeof record);
    memset(&tail, 0, sizeof tail);
    memset(&material, 0, sizeof material);
    memset(&rest, 0, sizeof rest);
    if (!data_root || !out || !out_n) return -1;
    if (under(data_root, "main/frontier/texture_materials.tsv", path, sizeof path) ||
        csv_load_delim(path, &csv, '\t') || csv.n < 2 || csv.rows[0].n != 9) goto done;
    for (i = 0; i < 9; ++i) if (strcmp(csv.rows[0].cell[i], fields[i]) != 0) goto done;
    for (row = 1; row < csv.n; ++row) {
        unsigned long index, selector, color;
        unsigned col;
        if (csv.rows[row].n != 9) goto done;
        if (parse_c_int(csv.rows[row].cell[0], &index, 1) || index != row ||
            parse_c_int(csv.rows[row].cell[1], &selector, 1) || selector > 65535ul) goto done;
        if (fe2_buf_byte(&material, (uint8_t)selector) || fe2_buf_byte(&material, (uint8_t)(selector >> 8))) goto done;
        for (col = 2; col < 9; ++col) {
            if (parse_c_int(csv.rows[row].cell[col], &color, 1) ||
                !((color <= 0xffful) || color == 0xfffful)) goto done;
            if (fe2_buf_byte(&material, (uint8_t)color) || fe2_buf_byte(&material, (uint8_t)(color >> 8))) goto done;
        }
    }
    if (!material.count) goto done;
    if (under(data_root, "main/frontier/coordinate_pair_rejection_masks.csv", path, sizeof path) ||
        append_units(path, &rest, 2, 0, 30) ||
        under(data_root, "main/frontier/main_render_parameter_tail.csv", path, sizeof path) ||
        append_units(path, &tail, 1, 0, 0) || tail.count < 177u ||
        under(data_root, "main/frontier/main_render_inline_curve.csv", path, sizeof path) ||
        append_units(path, &rest, 1, 0, 33) ||
        under(data_root, "video/scanline_shape_profiles.csv", path, sizeof path) ||
        append_units(path, &rest, 1, 1, 672) ||
        under(data_root, "main/frontier/paired_disc_outer_sizes.csv", path, sizeof path) ||
        append_units(path, &rest, 1, 0, 9) ||
        under(data_root, "main/frontier/paired_disc_inner_sizes.csv", path, sizeof path) ||
        append_units(path, &rest, 1, 0, 9)) goto done;
    if (append_u32(&record, (uint32_t)material.count) ||
        fe2_buf_add(&record, material.data, material.count) ||
        fe2_buf_add(&record, rest.data, 30) ||
        append_u32(&record, (uint32_t)(tail.count - 14u)) ||
        fe2_buf_add(&record, tail.data + 14, tail.count - 14u) ||
        fe2_buf_add(&record, rest.data + 30, 33) ||
        fe2_buf_add(&record, tail.data + 135, 42) ||
        fe2_buf_add(&record, rest.data + 63, rest.count - 63)) goto done;
    *out = record.data;
    *out_n = record.count;
    record.data = NULL;
    status = 0;
done:
    csv_free(&csv);
    fe2_buf_free(&record);
    fe2_buf_free(&tail);
    fe2_buf_free(&material);
    fe2_buf_free(&rest);
    return status;
}

static int read_idents(const char *path, char names[][64], unsigned cap, unsigned *count)
{
    char *text, *cursor;
    *count = 0;
    text = fe2_read_text(path, NULL);
    if (!text) return -1;
    cursor = text;
    while (*cursor) {
        char *eol = strchr(cursor, '\n');
        size_t len;
        if (eol) *eol = 0;
        len = strlen(cursor);
        if (len && cursor[len - 1] == '\r') cursor[--len] = 0;
        if (len) {
            if (*count >= cap || len >= 64) { free(text); return -1; }
            memcpy(names[*count], cursor, len + 1);
            ++*count;
        }
        if (!eol) break;
        cursor = eol + 1;
    }
    free(text);
    return 0;
}

static int decimal_word(const char *text, unsigned *out)
{
    const char *cursor = text;
    unsigned value = 0;
    if (!*cursor) return -1;
    while (*cursor) {
        if (*cursor < '0' || *cursor > '9' || value > 6553u) return -1;
        value = value * 10u + (unsigned)(*cursor - '0');
        if (value > 65535u) return -1;
        ++cursor;
    }
    *out = value;
    return 0;
}

int fe2_compile_offences(const char *data_root, uint8_t **out, size_t *out_n)
{
    char names[13][64];
    char path[512];
    fe2_csv csv;
    uint8_t image[1292];
    unsigned count, row, at;
    memset(&csv, 0, sizeof csv);
    if (!data_root || !out || !out_n) return -1;
    if (under(data_root, "text/offences/identifiers.txt", path, sizeof path) ||
        read_idents(path, names, 13, &count) || count != 13 ||
        strcmp(names[0], "first_fine") != 0 || strcmp(names[1], "additional_fine") != 0) return -1;
    if (under(data_root, "ownership/challenges.tsv", path, sizeof path) ||
        csv_load_delim(path, &csv, '\t') || csv.n != 302 || csv.rows[0].n != 4 ||
        strcmp(csv.rows[0].cell[0], "word") != 0 || strcmp(csv.rows[0].cell[1], "line") != 0 ||
        strcmp(csv.rows[0].cell[2], "page") != 0 || strcmp(csv.rows[0].cell[3], "answer") != 0) {
        csv_free(&csv);
        return -1;
    }
    for (row = 1; row < csv.n; ++row) {
        unsigned word, line, page;
        const char *answer;
        if (csv.rows[row].n != 4 || decimal_word(csv.rows[row].cell[0], &word) ||
            decimal_word(csv.rows[row].cell[1], &line) || decimal_word(csv.rows[row].cell[2], &page) ||
            word == 0 || word > 255 || line == 0 || line > 255 || page == 0 || page > 255) {
            csv_free(&csv);
            return -1;
        }
        answer = csv.rows[row].cell[3];
        if (answer[0] < 'A' || answer[0] > 'Z' || answer[1]) { csv_free(&csv); return -1; }
        image[(row - 1) * 4] = (uint8_t)word;
        image[(row - 1) * 4 + 1] = (uint8_t)line;
        image[(row - 1) * 4 + 2] = (uint8_t)page;
        image[(row - 1) * 4 + 3] = (uint8_t)(answer[0] - 'A');
    }
    csv_free(&csv);
    memset(&csv, 0, sizeof csv);
    if (under(data_root, "offences/parameters.csv", path, sizeof path) || csv_load(path, &csv)) return -1;
    if (csv.n != 12 || csv.rows[0].n != 7 ||
        strcmp(csv.rows[0].cell[0], "offence") != 0 || strcmp(csv.rows[0].cell[1], "score") != 0 ||
        strcmp(csv.rows[0].cell[2], "fine") != 0 || strcmp(csv.rows[0].cell[3], "alert") != 0 ||
        strcmp(csv.rows[0].cell[4], "message") != 0 || strcmp(csv.rows[0].cell[5], "find_police") != 0 ||
        strcmp(csv.rows[0].cell[6], "threshold") != 0) {
        csv_free(&csv);
        return -1;
    }
    at = 1204;
    for (row = 1; row < csv.n; ++row) {
        unsigned score, fine, threshold, flags = 0, col;
        static const unsigned bits[3] = {1u, 2u, 4u};
        if (csv.rows[row].n != 7 || strcmp(csv.rows[row].cell[0], names[row + 1]) != 0 ||
            decimal_word(csv.rows[row].cell[1], &score) || decimal_word(csv.rows[row].cell[2], &fine) ||
            decimal_word(csv.rows[row].cell[6], &threshold)) {
            csv_free(&csv);
            return -1;
        }
        for (col = 0; col < 3; ++col) {
            const char *flag = csv.rows[row].cell[3 + col];
            if (strcmp(flag, "yes") == 0) flags |= bits[col];
            else if (strcmp(flag, "no") != 0) { csv_free(&csv); return -1; }
        }
        image[at++] = (uint8_t)score; image[at++] = (uint8_t)(score >> 8);
        image[at++] = (uint8_t)fine; image[at++] = (uint8_t)(fine >> 8);
        image[at++] = (uint8_t)flags; image[at++] = (uint8_t)(flags >> 8);
        image[at++] = (uint8_t)threshold; image[at++] = (uint8_t)(threshold >> 8);
    }
    csv_free(&csv);
    return at == 1292 ? emit_bytes(out, out_n, image, 1292) : -1;
}

int fe2_compile_mining(const char *data_root, uint8_t **out, size_t *out_n)
{
    char prospects[40][64], commodities[31][64];
    char path[512];
    fe2_csv csv;
    uint8_t image[171];
    unsigned text_n, commodity_n, row, at = 0;
    memset(&csv, 0, sizeof csv);
    if (!data_root || !out || !out_n) return -1;
    if (under(data_root, "text/mining/identifiers.txt", path, sizeof path) ||
        read_idents(path, prospects, 40, &text_n) || text_n < 11) return -1;
    if (under(data_root, "text/market/strings.tsv", path, sizeof path) ||
        csv_load_delim(path, &csv, '\t') || csv.n != 31) return -1;
    for (commodity_n = 0; commodity_n < 31; ++commodity_n) {
        if (!csv.rows[commodity_n].n || strlen(csv.rows[commodity_n].cell[0]) >= 64) { csv_free(&csv); return -1; }
        memcpy(commodities[commodity_n], csv.rows[commodity_n].cell[0], strlen(csv.rows[commodity_n].cell[0]) + 1);
    }
    csv_free(&csv);
    memset(&csv, 0, sizeof csv);
    if (under(data_root, "mining/yields.csv", path, sizeof path) ||
        csv_load(path, &csv) || csv.n != 28) return -1;
    for (row = 1; row < csv.n; ++row) {
        unsigned index, reserve, category, assessment, i;
        if (csv.rows[row].n != 4 || decimal_word(csv.rows[row].cell[0], &index) || index != row - 1 ||
            decimal_word(csv.rows[row].cell[3], &reserve)) { csv_free(&csv); return -1; }
        category = 9;
        for (i = 0; i < 9; ++i) if (strcmp(csv.rows[row].cell[1], prospects[2 + i]) == 0) category = i;
        assessment = text_n;
        for (i = 0; i < text_n; ++i) if (strcmp(csv.rows[row].cell[2], prospects[i]) == 0) assessment = 0xa200u + i;
        if (category == 9 || assessment == text_n) { csv_free(&csv); return -1; }
        image[at++] = (uint8_t)category; image[at++] = (uint8_t)(category >> 8);
        image[at++] = (uint8_t)assessment; image[at++] = (uint8_t)(assessment >> 8);
        image[at++] = (uint8_t)reserve; image[at++] = (uint8_t)(reserve >> 8);
    }
    csv_free(&csv);
    memset(&csv, 0, sizeof csv);
    if (under(data_root, "mining/categories.csv", path, sizeof path) ||
        csv_load(path, &csv) || csv.n != 10) return -1;
    for (row = 1; row < csv.n; ++row) {
        unsigned i, commodity = 31;
        if (csv.rows[row].n != 2 || strcmp(csv.rows[row].cell[0], prospects[1 + row]) != 0) {
            csv_free(&csv);
            return -1;
        }
        for (i = 0; i < 31; ++i) if (strcmp(csv.rows[row].cell[1], commodities[i]) == 0) commodity = i;
        if (commodity == 31) { csv_free(&csv); return -1; }
        image[at++] = (uint8_t)commodity;
    }
    csv_free(&csv);
    return at == 171 ? emit_bytes(out, out_n, image, 171) : -1;
}

int fe2_compile_commander(const char *data_root, uint8_t **out, size_t *out_n)
{
    static const char *preferences[] = {
        "detail_low", "detail_medium", "detail_high", "detail_very_high",
        "space_dust", "background_stars", "external_view_lock",
        "reverse_left_right", "reverse_up_down", "elite_controls",
        "targeting_tunnels", "ship_targeting_tunnels", "filename_extensions",
        "sound_effects", "engine_sound", "station_sound", "wind_sound", "icon_beeps",
        "music", "hyperspace_music", "docking_music", "battle_music", "continuous_music",
        "track_hall_of_the_mountain_king", "track_frontier_main_theme", "track_baba_yaga",
        "track_night_on_the_bare_mountain", "track_frontier_second_theme",
        "track_ride_of_the_valkyries", "track_blue_danube", "track_great_gates_of_kiev",
        "reserved", "reserved"
    };
    static const unsigned keys[] = {
        0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0xa9, 0x9a, 0x9b, 0x9c,
        0x8a, 0x8d, 0xa8, 0xa4, 0x8e, 0x8b, 0xa5, 0xa6, 0xa7, 0x8c, 0x91, 0x92, 0x93,
        0x94, 0x95, 0x96, 0x97, 0x98
    };
    fe2_csv csv;
    fe2_buf record;
    char path[512];
    size_t row;
    unsigned i;
    int status = -1;
    memset(&csv, 0, sizeof csv);
    memset(&record, 0, sizeof record);
    if (!data_root || !out || !out_n) return -1;
    if (under(data_root, "preferences/default_preferences.csv", path, sizeof path) ||
        csv_load(path, &csv) || csv.n != 34 || csv.rows[0].n != 2 ||
        strcmp(csv.rows[0].cell[0], "preference") != 0 || strcmp(csv.rows[0].cell[1], "enabled") != 0) goto done;
    for (row = 1; row < csv.n; ++row) {
        uint8_t enabled;
        if (csv.rows[row].n != 2 || strcmp(csv.rows[row].cell[0], preferences[row - 1]) != 0) goto done;
        if (strcmp(csv.rows[row].cell[1], "on") == 0) enabled = 0xff;
        else if (strcmp(csv.rows[row].cell[1], "off") == 0) enabled = 0;
        else goto done;
        if (fe2_buf_byte(&record, enabled)) goto done;
    }
    csv_free(&csv);
    memset(&csv, 0, sizeof csv);
    if (under(data_root, "preferences/preference_widgets.csv", path, sizeof path) ||
        csv_load(path, &csv) || csv.n != 32 || csv.rows[0].n != 5) goto done;
    for (row = 1; row < csv.n; ++row) {
        unsigned col, value;
        if (csv.rows[row].n != 5 || strcmp(csv.rows[row].cell[4], preferences[row - 1]) != 0) goto done;
        for (col = 0; col < 4; ++col) {
            if (decimal_word(csv.rows[row].cell[col], &value) || fe2_buf_byte(&record, (uint8_t)value) ||
                fe2_buf_byte(&record, (uint8_t)(value >> 8))) goto done;
        }
        if (fe2_buf_byte(&record, (uint8_t)keys[row - 1]) || fe2_buf_byte(&record, (uint8_t)(keys[row - 1] >> 8)))
            goto done;
    }
    if (fe2_buf_byte(&record, 0xff) || fe2_buf_byte(&record, 0xff)) goto done;
    csv_free(&csv);
    memset(&csv, 0, sizeof csv);
    if (under(data_root, "preferences/shape_detail_presets.csv", path, sizeof path) ||
        csv_load(path, &csv) || csv.n != 5) goto done;
    for (row = 1; row < 5; ++row)
        if (csv.rows[row].n != 1 || strcmp(csv.rows[row].cell[0], preferences[row - 1]) != 0) goto done;
    for (i = 0; i < 4; ++i) {
        unsigned shift;
        for (shift = 0; shift < 4; ++shift)
            if (fe2_buf_byte(&record, shift == i ? 0xff : 0)) goto done;
    }
    csv_free(&csv);
    memset(&csv, 0, sizeof csv);
    if (fe2_buf_add(&record, "COMMANDR", 9)) goto done;
    for (i = 0; i < 134; ++i) if (fe2_buf_byte(&record, 0)) goto done;
    memset(&csv, 0, sizeof csv);
    if (under(data_root, "preferences/filename_edit_widgets.csv", path, sizeof path) ||
        csv_load(path, &csv) || csv.n != 3 || csv.rows[0].n != 5 ||
        strcmp(csv.rows[1].cell[4], "save_file") != 0 || strcmp(csv.rows[2].cell[4], "edit_name") != 0) goto done;
    for (row = 1; row < 3; ++row) {
        unsigned col, value;
        static const unsigned file_keys[] = {0x0d, 0xe0};
        for (col = 0; col < 4; ++col) {
            if (decimal_word(csv.rows[row].cell[col], &value) || fe2_buf_byte(&record, (uint8_t)value) ||
                fe2_buf_byte(&record, (uint8_t)(value >> 8))) goto done;
        }
        if (fe2_buf_byte(&record, (uint8_t)file_keys[row - 1]) || fe2_buf_byte(&record, 0)) goto done;
    }
    {
        static const uint8_t tail[] = {
            0xff, 0xff, 0x3e, 0x89, 0x36, 0x89, 0x37, 0x89, 0x38, 0x89,
            'J', 'a', 'm', 'e', 's', 'o', 'n', 0,
            '.', '_', '0', '1', 0, 92,
            '(', 'P', 'a', 'r', 'e', 'n', 't', ' ', 'D', 'i', 'r', ')', 0,
            0, 0, 0, 0, 0, 0
        };
        if (fe2_buf_add(&record, tail, sizeof tail)) goto done;
    }
    if (record.count != 567) goto done;
    *out = record.data;
    *out_n = record.count;
    record.data = NULL;
    status = 0;
done:
    csv_free(&csv);
    fe2_buf_free(&record);
    return status;
}
