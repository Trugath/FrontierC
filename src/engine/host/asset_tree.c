/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "asset_tree.h"

#include "asset_files.h"
#include "asset_image.h"
#include "asset_ini.h"
#include "asset_adlib.h"
#include "asset_tables.h"
#include "asset_model.h"
#include "asset_util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

static int add_record(fe2_tree_archive *tree, const char *name, uint8_t *data, size_t count)
{
    char **names;
    uint8_t **blobs;
    fe2_asset_part *parts;
    if (!name[0] || strlen(name) > 255 || count > 0xffffffffu) {
        free(data);
        return fe2_asset_fail("Bad asset name %s", name);
    }
    names = (char **)realloc(tree->names, (tree->count + 1) * sizeof *names);
    if (!names) { free(data); return -1; }
    tree->names = names;
    blobs = (uint8_t **)realloc(tree->blobs, (tree->count + 1) * sizeof *blobs);
    if (!blobs) { free(data); return -1; }
    tree->blobs = blobs;
    parts = (fe2_asset_part *)realloc(tree->parts, (tree->count + 1) * sizeof *parts);
    if (!parts) { free(data); return -1; }
    tree->parts = parts;
    tree->names[tree->count] = (char *)malloc(strlen(name) + 1);
    if (!tree->names[tree->count]) { free(data); return -1; }
    memcpy(tree->names[tree->count], name, strlen(name) + 1);
    tree->blobs[tree->count] = data;
    tree->parts[tree->count].name = tree->names[tree->count];
    tree->parts[tree->count].data = data;
    tree->parts[tree->count].count = (uint32_t)count;
    ++tree->count;
    return 0;
}

static int add_copy(fe2_tree_archive *tree, const char *name, const void *data, size_t count)
{
    uint8_t *copy = (uint8_t *)malloc(count ? count : 1);
    if (!copy) return -1;
    if (count) memcpy(copy, data, count);
    return add_record(tree, name, copy, count);
}

static int parse_i64(const char *text, long long *out)
{
    char *end;
    long long value = strtoll(text, &end, 0);
    if (!text || end == text || *end) return -1;
    *out = value;
    return 0;
}

static int read_font(const char *path, fe2_buf *out)
{
    char *text = fe2_read_text(path, NULL), *cursor;
    int glyphs = 0, in_glyph = 0, chars = -1;
    char rows[9][3];
    int row_n = 0, encoding = -1, advance = -1, have_bitmap = 0;
    if (!text) return fe2_asset_fail("Could not read %s", path);
    cursor = text;
    if (strncmp(cursor, "STARTFONT 2.1", 13) != 0) { free(text); return fe2_asset_fail("Expected a BDF 2.1 font"); }
    while (*cursor) {
        char *eol = strchr(cursor, '\n');
        if (eol) *eol = 0;
        if (cursor[0] && cursor[strlen(cursor) - 1] == '\r') cursor[strlen(cursor) - 1] = 0;
        if (!strncmp(cursor, "CHARS ", 6)) chars = atoi(cursor + 6);
        if (!strncmp(cursor, "STARTCHAR ", 10)) {
            if (in_glyph) { free(text); return fe2_asset_fail("Nested BDF glyph"); }
            in_glyph = 1;
            row_n = 0;
            encoding = advance = -1;
            have_bitmap = 0;
        } else if (!strcmp(cursor, "ENDCHAR")) {
            int i;
            if (!in_glyph || encoding != glyphs || advance < 0 || advance > 255 || row_n != 9) {
                free(text); return fe2_asset_fail("Font requires contiguous glyph IDs, 8x9 cells and byte advances");
            }
            for (i = 0; i < 9; ++i) {
                unsigned value;
                if (strlen(rows[i]) != 2 || sscanf(rows[i], "%2x", &value) != 1 || fe2_buf_byte(out, (uint8_t)value)) {
                    free(text); return fe2_asset_fail("Glyph bitmap requires nine byte rows");
                }
            }
            if (fe2_buf_byte(out, (uint8_t)advance)) { free(text); return -1; }
            ++glyphs;
            in_glyph = 0;
        } else if (in_glyph && !strncmp(cursor, "ENCODING ", 9)) encoding = atoi(cursor + 9);
        else if (in_glyph && !strncmp(cursor, "DWIDTH ", 7)) advance = atoi(cursor + 7);
        else if (in_glyph && !strcmp(cursor, "BITMAP")) have_bitmap = 1;
        else if (in_glyph && have_bitmap && row_n < 9) {
            snprintf(rows[row_n], sizeof rows[row_n], "%s", cursor);
            ++row_n;
        }
        if (!eol) break;
        cursor = eol + 1;
    }
    free(text);
    if (in_glyph || chars != glyphs || glyphs != 115) return fe2_asset_fail("Font requires 115 complete glyphs");
    return 0;
}

static int read_palette(const char *path, fe2_buf *out)
{
    char *text = fe2_read_text(path, NULL), *cursor;
    int first = 1;
    if (!text) return fe2_asset_fail("Could not read %s", path);
    cursor = text;
    while (*cursor) {
        char *eol = strchr(cursor, '\n');
        int r, g, b;
        if (eol) *eol = 0;
        if (cursor[0] && cursor[strlen(cursor) - 1] == '\r') cursor[strlen(cursor) - 1] = 0;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (first) {
            if (strcmp(cursor, "GIMP Palette") != 0) { free(text); return fe2_asset_fail("Expected a GIMP palette"); }
            first = 0;
        } else if (*cursor && *cursor != '#' && strncmp(cursor, "Name:", 5) && strncmp(cursor, "Columns:", 8)) {
            if (sscanf(cursor, "%d %d %d", &r, &g, &b) != 3 || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
                free(text); return fe2_asset_fail("Palette components must be RGB8 values");
            }
            if (fe2_buf_byte(out, (uint8_t)((r * 63 + 127) / 255)) ||
                fe2_buf_byte(out, (uint8_t)((g * 63 + 127) / 255)) ||
                fe2_buf_byte(out, (uint8_t)((b * 63 + 127) / 255))) { free(text); return -1; }
        }
        if (!eol) break;
        cursor = eol + 1;
    }
    free(text);
    return 0;
}

static int decode_text(const char *value, fe2_buf *out)
{
    const char *cursor = value;
    while (*cursor) {
        if (*cursor == '\\') {
            unsigned byte;
            if (cursor[1] == '\\') { if (fe2_buf_byte(out, 92)) return -1; cursor += 2; continue; }
            if (cursor[1] != 'x' || !isxdigit((unsigned char)cursor[2]) || !isxdigit((unsigned char)cursor[3]) ||
                sscanf(cursor + 2, "%2x", &byte) != 1) return fe2_asset_fail("Expected \\\\ or \\xHH escape");
            if (fe2_buf_byte(out, (uint8_t)byte)) return -1;
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

static int compile_catalog(const char *catalog_path, const char *root, fe2_buf *payload, unsigned *entries,
                           unsigned *kind, unsigned *slot, unsigned *mask, char *module, size_t module_cap)
{
    static const char *names[] = {
        "systems", "chart", "flight", "market", "intro", "names", "commander", "status", "offences", "mining", "interaction"
    };
    static const unsigned slots[] = {0, 2, 3, 7, 8, 9, 10, 14, 15, 17, 18};
    fe2_ini ini;
    fe2_ini_section *section;
    char base[4096], path[4096], *text, *cursor;
    fe2_buf strings;
    unsigned offsets[4096];
    size_t count = 0, i, at;
    const char *dir = strrchr(catalog_path, '/');
    (void)root;
    memset(&ini, 0, sizeof ini);
    memset(&strings, 0, sizeof strings);
    if (fe2_ini_load(&ini, catalog_path)) return fe2_asset_fail("Could not read %s", catalog_path);
    section = fe2_ini_section_find(&ini, "strings");
    if (!section || !fe2_ini_get(section, "module") || !fe2_ini_get(section, "entries") ||
        snprintf(module, module_cap, "%s", fe2_ini_get(section, "module")) >= (int)module_cap) {
        fe2_ini_free(&ini); return fe2_asset_fail("Invalid module name");
    }
    {
        const char *slash = strrchr(catalog_path, '/');
        const char *back = strrchr(catalog_path, '\\');
        const char *cut = !slash ? back : !back ? slash : (slash > back ? slash : back);
        size_t n = cut ? (size_t)(cut - catalog_path) : 0;
        if (!n || n >= sizeof base) { fe2_ini_free(&ini); return -1; }
        memcpy(base, catalog_path, n);
        base[n] = 0;
    }
    if (fe2_path_join(path, sizeof path, base, fe2_ini_get(section, "entries"))) { fe2_ini_free(&ini); return -1; }
    text = fe2_read_text(path, NULL);
    if (!text) { fe2_ini_free(&ini); return fe2_asset_fail("Could not read %s", path); }
    cursor = text;
    while (*cursor) {
        char *eol = strchr(cursor, '\n');
        char *tab;
        fe2_buf entry;
        if (eol) *eol = 0;
        if (cursor[0] && cursor[strlen(cursor) - 1] == '\r') cursor[strlen(cursor) - 1] = 0;
        tab = strchr(cursor, '\t');
        if (!tab || tab == cursor) { free(text); fe2_buf_free(&strings); fe2_ini_free(&ini); return fe2_asset_fail("Expected unique assembly identifiers"); }
        *tab = 0;
        memset(&entry, 0, sizeof entry);
        if (decode_text(tab + 1, &entry) || count == 4096) {
            free(text); fe2_buf_free(&entry); fe2_buf_free(&strings); fe2_ini_free(&ini); return -1;
        }
        offsets[count++] = 0;
        if (fe2_buf_add(&strings, entry.data, entry.count) || fe2_buf_byte(&strings, 0)) {
            free(text); fe2_buf_free(&entry); fe2_buf_free(&strings); fe2_ini_free(&ini); return -1;
        }
        fe2_buf_free(&entry);
        if (!eol) break;
        cursor = eol + 1;
        if (!*cursor) break;
    }
    free(text);
    if (fe2_ini_get(section, "count")) {
        long long expected;
        if (parse_i64(fe2_ini_get(section, "count"), &expected) || expected != (long long)count) {
            fe2_buf_free(&strings); fe2_ini_free(&ini); return fe2_asset_fail("Text entry count differs from catalog");
        }
    }
    at = count * 2;
    {
        size_t used = 0;
        for (i = 0; i < count; ++i) {
            offsets[i] = (unsigned)at;
            while (used < strings.count && strings.data[used]) ++used;
            ++used;
            at = count * 2 + used;
        }
    }
    for (i = 0; i < count; ++i) if (fe2_buf_u16(payload, offsets[i])) { fe2_buf_free(&strings); fe2_ini_free(&ini); return -1; }
    if (fe2_buf_add(payload, strings.data, strings.count)) { fe2_buf_free(&strings); fe2_ini_free(&ini); return -1; }
    fe2_buf_free(&strings);
    *entries = (unsigned)count;
    if (!strcmp(module, "frontier")) { *kind = 1; *slot = 0; *mask = 0; }
    else {
        size_t n;
        *kind = 2; *mask = 511;
        for (n = 0; n < sizeof names / sizeof *names; ++n) if (!strcmp(module, names[n])) { *slot = slots[n]; break; }
        if (n == sizeof names / sizeof *names) { fe2_ini_free(&ini); return fe2_asset_fail("No text slot for %s", module); }
    }
    fe2_ini_free(&ini);
    (void)dir;
    return 0;
}

static int compare_paths(const void *left, const void *right)
{
    return strcmp(*(char *const *)left, *(char *const *)right);
}

static int list_catalogs(const char *text_root, char ***out, size_t *count)
{
    char **names = NULL;
    size_t n = 0;
#ifdef _WIN32
    char pattern[4096];
    WIN32_FIND_DATAA found;
    HANDLE handle;
    if (snprintf(pattern, sizeof pattern, "%s\\*", text_root) >= (int)sizeof pattern) return -1;
    handle = FindFirstFileA(pattern, &found);
    if (handle == INVALID_HANDLE_VALUE) return fe2_asset_fail("Could not read %s", text_root);
    do {
        char path[4096];
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (!strcmp(found.cFileName, ".") || !strcmp(found.cFileName, "..")) continue;
        if (snprintf(path, sizeof path, "%s/%s/catalog.ini", text_root, found.cFileName) >= (int)sizeof path) {
            FindClose(handle); return -1;
        }
        if (!fe2_file_exists(path)) continue;
        names = (char **)realloc(names, (n + 1) * sizeof *names);
        names[n] = (char *)malloc(strlen(path) + 1);
        if (!names || !names[n]) { FindClose(handle); return -1; }
        memcpy(names[n], path, strlen(path) + 1);
        ++n;
    } while (FindNextFileA(handle, &found));
    FindClose(handle);
#else
    DIR *dir = opendir(text_root);
    struct dirent *entry;
    if (!dir) return fe2_asset_fail("Could not read %s", text_root);
    while ((entry = readdir(dir)) != NULL) {
        char path[4096];
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (snprintf(path, sizeof path, "%s/%s/catalog.ini", text_root, entry->d_name) >= (int)sizeof path) {
            closedir(dir); return -1;
        }
        if (!fe2_file_exists(path)) continue;
        names = (char **)realloc(names, (n + 1) * sizeof *names);
        names[n] = (char *)malloc(strlen(path) + 1);
        if (!names || !names[n]) { closedir(dir); return -1; }
        memcpy(names[n], path, strlen(path) + 1);
        ++n;
    }
    closedir(dir);
#endif
    if (n > 1) qsort(names, n, sizeof *names, compare_paths);
    *out = names;
    *count = n;
    return 0;
}

static int text_entry(fe2_buf *out, const char *name, unsigned entries, unsigned kind, unsigned slot, unsigned mask)
{
    size_t n = strlen(name);
    if (!n || n > 255 || fe2_buf_byte(out, (uint8_t)n) || fe2_buf_add(out, name, n) ||
        fe2_buf_u16(out, entries) || fe2_buf_u16(out, kind) || fe2_buf_u16(out, slot) || fe2_buf_u16(out, mask))
        return -1;
    return 0;
}

static int unsigned_list(const char *text, unsigned *values, unsigned capacity, unsigned *count, unsigned bits)
{
    const char *cursor = text;
    *count = 0;
    while (*cursor) {
        char *end;
        unsigned long value;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',' || *cursor == '\n' || *cursor == '\r') ++cursor;
        if (!*cursor) break;
        if (*count == capacity) return -1;
        value = strtoul(cursor, &end, 0);
        if (end == cursor || value > 0xfffffffful || (bits < 32 && value >= (1ul << bits))) return -1;
        values[(*count)++] = (unsigned)value;
        cursor = end;
    }
    return 0;
}

static int compile_intro_object(const char *path, uint8_t out[286])
{
    fe2_ini ini;
    fe2_ini_section *orientation, *render, *state;
    const char *rows, *words_text, *field;
    unsigned values[9], count, i, slot;
    uint16_t words[143];
    uint8_t seen[143];
    memset(&ini, 0, sizeof ini);
    memset(words, 0, sizeof words);
    memset(seen, 0, sizeof seen);
    if (fe2_ini_load(&ini, path)) return fe2_asset_fail("Could not read %s", path);
    orientation = fe2_ini_section_find(&ini, "orientation");
    render = fe2_ini_section_find(&ini, "render");
    state = fe2_ini_section_find(&ini, "state");
    if (ini.count != 3 || !orientation || !render || !state || orientation->count != 1 ||
        render->count != 6 || state->count != 1) {
        fe2_ini_free(&ini);
        return fe2_asset_fail("Invalid intro object sections");
    }
    rows = fe2_ini_get(orientation, "rows");
    if (!rows || unsigned_list(rows, values, 9, &count, 16) || count != 9) {
        fe2_ini_free(&ini);
        return fe2_asset_fail("Expected three rows of three orientation words");
    }
    for (i = 0; i < 9; ++i) words[i] = (uint16_t)values[i];
    field = fe2_ini_get(render, "word_12");
    if (!field || unsigned_list(field, values, 1, &count, 16) || count != 1) { fe2_ini_free(&ini); return -1; }
    words[0x12 / 2] = (uint16_t)values[0];
    field = fe2_ini_get(render, "phase");
    if (!field || unsigned_list(field, values, 1, &count, 16) || count != 1) { fe2_ini_free(&ini); return -1; }
    words[0x72 / 2] = (uint16_t)values[0];
    field = fe2_ini_get(render, "model_slot");
    if (!field || unsigned_list(field, &slot, 1, &count, 16) || count != 1 || slot >= 240) {
        fe2_ini_free(&ini);
        return fe2_asset_fail("Intro model slot outside the directory");
    }
    words[0x5a / 2] = (uint16_t)(slot * 2u);
    {
        static const struct { const char *key; unsigned offset; } offsets[] = {
            {"offset_x", 0x14}, {"offset_y", 0x18}, {"offset_z", 0x1c}
        };
        for (i = 0; i < 3; ++i) {
            unsigned wide[1];
            field = fe2_ini_get(render, offsets[i].key);
            if (!field || unsigned_list(field, wide, 1, &count, 32) || count != 1) {
                fe2_ini_free(&ini);
                return fe2_asset_fail("Intro render offset exceeds its width");
            }
            words[offsets[i].offset / 2] = (uint16_t)wide[0];
            words[offsets[i].offset / 2 + 1] = (uint16_t)(wide[0] >> 16);
        }
    }
    words_text = fe2_ini_get(state, "words");
    if (!words_text) { fe2_ini_free(&ini); return fe2_asset_fail("Invalid intro object payload fields"); }
    {
        const char *cursor = words_text;
        while (*cursor) {
            char line[128];
            unsigned pair[2];
            size_t n = 0;
            while (*cursor == '\n' || *cursor == '\r') ++cursor;
            if (!*cursor) break;
            while (cursor[n] && cursor[n] != '\n' && cursor[n] != '\r' && n + 1 < sizeof line) {
                line[n] = cursor[n];
                ++n;
            }
            if (cursor[n] && cursor[n] != '\n' && cursor[n] != '\r') { fe2_ini_free(&ini); return -1; }
            line[n] = 0;
            cursor += n;
            if (!line[0]) continue;
            if (unsigned_list(line, pair, 2, &count, 16) || count != 2) {
                fe2_ini_free(&ini);
                return fe2_asset_fail("Expected state word offset and value");
            }
            if (pair[0] % 2 || pair[0] < 0x20 || pair[0] >= 0x11e || pair[0] == 0x5a || pair[0] == 0x72 ||
                seen[pair[0] / 2]) {
                fe2_ini_free(&ini);
                return fe2_asset_fail("Invalid, duplicated or reserved object state offset");
            }
            seen[pair[0] / 2] = 1;
            words[pair[0] / 2] = (uint16_t)pair[1];
        }
    }
    fe2_ini_free(&ini);
    for (i = 0; i < 143; ++i) {
        out[2 * i] = (uint8_t)words[i];
        out[2 * i + 1] = (uint8_t)(words[i] >> 8);
    }
    return 0;
}

static int add_intro_scenes(fe2_tree_archive *tree, const char *data_root)
{
    fe2_ini ini;
    char path[4096];
    uint8_t *orbit, *scenes, object[286];
    uint8_t have_orbit = 0, have_scene[22];
    unsigned ranges[23][2], range_n = 0, i, j;
    size_t s;
    memset(&ini, 0, sizeof ini);
    memset(have_scene, 0, sizeof have_scene);
    orbit = (uint8_t *)calloc(286, 1);
    scenes = (uint8_t *)calloc(22u * 286u, 1);
    if (!orbit || !scenes) { free(orbit); free(scenes); return -1; }
    if (snprintf(path, sizeof path, "%s/intro/objects.ini", data_root) >= (int)sizeof path ||
        fe2_ini_load(&ini, path)) {
        free(orbit); free(scenes);
        return fe2_asset_fail("Could not read intro objects");
    }
    for (s = 0; s < ini.count; ++s) {
        const char *name = ini.sections[s].name;
        const char *file = fe2_ini_get(&ini.sections[s], "file");
        const char *start_text = fe2_ini_get(&ini.sections[s], "start");
        const char *end_text = fe2_ini_get(&ini.sections[s], "end");
        unsigned bounds[1], count, start, end;
        int index = -2;
        if (!file || !start_text || !end_text || ini.sections[s].count != 3) {
            fe2_ini_free(&ini); free(orbit); free(scenes);
            return fe2_asset_fail("Invalid intro object name");
        }
        if (!strcmp(name, "initial_scene_object")) index = -1;
        else if (strlen(name) == 21 && !strncmp(name, "intro_scene_object_", 19) &&
                 name[19] >= '0' && name[19] <= '9' && name[20] >= '0' && name[20] <= '9')
            index = (name[19] - '0') * 10 + (name[20] - '0');
        if (index < -1 || index >= 22) {
            fe2_ini_free(&ini); free(orbit); free(scenes);
            return fe2_asset_fail("Invalid intro object name");
        }
        if (unsigned_list(start_text, bounds, 1, &count, 16) || count != 1) {
            fe2_ini_free(&ini); free(orbit); free(scenes); return -1;
        }
        start = bounds[0];
        if (unsigned_list(end_text, bounds, 1, &count, 16) || count != 1 || end_text[0] == 0) {
            fe2_ini_free(&ini); free(orbit); free(scenes); return -1;
        }
        end = bounds[0];
        if (end - start != 286 || start >= end) {
            fe2_ini_free(&ini); free(orbit); free(scenes);
            return fe2_asset_fail("Intro render objects must occupy 286 bytes");
        }
        if (snprintf(path, sizeof path, "%s/intro/%s", data_root, file) >= (int)sizeof path ||
            compile_intro_object(path, object)) {
            fe2_ini_free(&ini); free(orbit); free(scenes); return -1;
        }
        if (index < 0) {
            if (have_orbit) { fe2_ini_free(&ini); free(orbit); free(scenes); return -1; }
            memcpy(orbit, object, 286);
            have_orbit = 1;
        } else {
            if (have_scene[index]) { fe2_ini_free(&ini); free(orbit); free(scenes); return -1; }
            memcpy(scenes + (size_t)index * 286u, object, 286);
            have_scene[index] = 1;
        }
        if (range_n == 23) { fe2_ini_free(&ini); free(orbit); free(scenes); return -1; }
        ranges[range_n][0] = start;
        ranges[range_n][1] = end;
        ++range_n;
    }
    fe2_ini_free(&ini);
    if (!have_orbit || range_n != 23) {
        free(orbit); free(scenes);
        return fe2_asset_fail("Intro object catalog must define all 23 objects");
    }
    for (i = 0; i < 22; ++i) if (!have_scene[i]) {
        free(orbit); free(scenes);
        return fe2_asset_fail("Intro object catalog must define all 23 objects");
    }
    for (i = 0; i < range_n; ++i) for (j = i + 1; j < range_n; ++j)
        if (ranges[i][0] < ranges[j][1] && ranges[j][0] < ranges[i][1]) {
            free(orbit); free(scenes);
            return fe2_asset_fail("Intro object ranges overlap");
        }
    if (add_record(tree, "intro/orbit", orbit, 286)) { free(scenes); return -1; }
    if (add_record(tree, "intro/scenes", scenes, 22u * 286u)) return -1;
    return 0;
}

int fe2_pack_tree(const char *data_root, fe2_tree_archive *tree)
{
    char path[4096], names_path[4096];
    fe2_bank_image banks[2];
    fe2_buf font, palette, info, builtin, payload;
    const char *bank_names[2] = {"models/frontier/models.ini", "models/frontier_auxiliary/models.ini"};
    int b, status = -1;
    memset(tree, 0, sizeof *tree);
    memset(banks, 0, sizeof banks);
    memset(&font, 0, sizeof font);
    memset(&palette, 0, sizeof palette);
    memset(&info, 0, sizeof info);
    memset(&builtin, 0, sizeof builtin);
    if (snprintf(names_path, sizeof names_path, "%s/text/frontier/strings.tsv", data_root) >= (int)sizeof names_path)
        return -1;
    if (!fe2_file_exists(names_path)) names_path[0] = 0;
    for (b = 0; b < 2; ++b) {
        size_t i;
        char record[64];
        if (snprintf(path, sizeof path, "%s/%s", data_root, bank_names[b]) >= (int)sizeof path) goto done;
        if (fe2_compile_bank(path, names_path[0] ? names_path : NULL, &banks[b])) goto done;
        if (add_copy(tree, b ? "bank/1" : "bank/0", banks[b].image, banks[b].image_count)) goto done;
        {
            uint8_t *offsets = (uint8_t *)malloc(480);
            if (!offsets) goto done;
            for (i = 0; i < 240; ++i) {
                offsets[2 * i] = (uint8_t)banks[b].offsets[i];
                offsets[2 * i + 1] = (uint8_t)(banks[b].offsets[i] >> 8);
            }
            if (add_record(tree, b ? "model-offsets/1" : "model-offsets/0", offsets, 480)) goto done;
        }
        for (i = 0; i < banks[b].count; ++i) {
            if (snprintf(record, sizeof record, "model/%d/%u", b, banks[b].models[i].slot) >= (int)sizeof record) goto done;
            if (add_copy(tree, record, banks[b].models[i].data, banks[b].models[i].count)) goto done;
        }
    }
    if (snprintf(path, sizeof path, "%s/video/font.bdf", data_root) >= (int)sizeof path || read_font(path, &font) ||
        add_copy(tree, "font", font.data, font.count)) goto done;
    if (snprintf(path, sizeof path, "%s/video/default.gpl", data_root) >= (int)sizeof path || read_palette(path, &palette) ||
        add_copy(tree, "palette", palette.data, palette.count)) goto done;
    {
        char *widths_text, *cursor;
        uint8_t widths[256];
        size_t n = 0;
        memset(widths, 0, sizeof widths);
        if (snprintf(path, sizeof path, "%s/main/frontier/glyph_widths.csv", data_root) >= (int)sizeof path) goto done;
        widths_text = fe2_read_text(path, NULL);
        if (!widths_text) { fe2_asset_fail("Could not read %s", path); goto done; }
        for (cursor = widths_text; *cursor; ++cursor) if (*cursor == '\n' || *cursor == '\r') *cursor = ',';
        cursor = widths_text;
        while (*cursor) {
            char *comma = strchr(cursor, ',');
            long long value;
            if (comma) *comma = 0;
            if (*cursor) {
                if (n == 256 || parse_i64(cursor, &value) || value < 0 || value > 255) {
                    free(widths_text); fe2_asset_fail("Unexpected glyph widths"); goto done;
                }
                widths[n++] = (uint8_t)value;
            }
            if (!comma) break;
            cursor = comma + 1;
        }
        free(widths_text);
        if (add_copy(tree, "widths", widths, 256)) goto done;
    }
    {
        const char *which[2] = {"one", "two"};
        size_t i;
        if (fe2_buf_u16(&builtin, 4)) goto done;
        for (i = 0; i < 2; ++i) {
            char *text, *start, *end;
            if (snprintf(path, sizeof path, "%s/main/frontier/greeting_%s.txt", data_root, which[i]) >= (int)sizeof path) goto done;
            text = fe2_read_text(path, NULL);
            if (!text) { fe2_asset_fail("Could not read %s", path); goto done; }
            start = text;
            while (*start && isspace((unsigned char)*start)) ++start;
            end = start + strlen(start);
            while (end > start && isspace((unsigned char)end[-1])) --end;
            if (i == 0 && fe2_buf_u16(&builtin, (unsigned)(4 + (size_t)(end - start) + 1))) { free(text); goto done; }
            if (fe2_buf_add(&builtin, start, (size_t)(end - start)) || fe2_buf_byte(&builtin, 0)) { free(text); goto done; }
            free(text);
        }
        if (add_copy(tree, "text/builtin", builtin.data, builtin.count)) goto done;
    }
    if (text_entry(&info, "builtin", 2, 0, 0, 0)) goto done;
    {
        char catalogs_root[4096], **catalogs = NULL;
        size_t catalogs_n = 0, i;
        if (snprintf(catalogs_root, sizeof catalogs_root, "%s/text", data_root) >= (int)sizeof catalogs_root) goto done;
        if (list_catalogs(catalogs_root, &catalogs, &catalogs_n)) goto done;
        for (i = 0; i < catalogs_n; ++i) {
            char module[64], record[96];
            unsigned entries = 0, kind = 0, slot = 0, mask = 0;
            memset(&payload, 0, sizeof payload);
            if (compile_catalog(catalogs[i], data_root, &payload, &entries, &kind, &slot, &mask, module, sizeof module) ||
                snprintf(record, sizeof record, "text/%s", module) >= (int)sizeof record ||
                add_copy(tree, record, payload.data, payload.count) ||
                text_entry(&info, module, entries, kind, slot, mask)) {
                fe2_buf_free(&payload);
                for (; i < catalogs_n; ++i) free(catalogs[i]);
                free(catalogs);
                goto done;
            }
            fe2_buf_free(&payload);
            free(catalogs[i]);
        }
        free(catalogs);
    }
    if (add_copy(tree, "text-info", info.data, info.count)) goto done;
    {
        fe2_ini graphics;
        size_t s;
        memset(&graphics, 0, sizeof graphics);
        if (snprintf(path, sizeof path, "%s/graphics/el2g.ini", data_root) >= (int)sizeof path || fe2_ini_load(&graphics, path)) {
            fe2_asset_fail("Could not read %s", path); goto done;
        }
        for (s = 0; s < graphics.count; ++s) {
            fe2_ini_section *section = &graphics.sections[s];
            fe2_image image;
            fe2_buf packed, record;
            char image_path[4096];
            long long flags, ox, oy;
            int ident = 0;
            const char *image_name = fe2_ini_get(section, "image");
            memset(&image, 0, sizeof image);
            memset(&packed, 0, sizeof packed);
            memset(&record, 0, sizeof record);
            if (sscanf(section->name, "%*s %d", &ident) != 1 || !image_name ||
                !fe2_ini_get(section, "flags") || !fe2_ini_get(section, "offset_x") || !fe2_ini_get(section, "offset_y") ||
                parse_i64(fe2_ini_get(section, "flags"), &flags) || parse_i64(fe2_ini_get(section, "offset_x"), &ox) ||
                parse_i64(fe2_ini_get(section, "offset_y"), &oy) || ox < -32768 || ox > 32767 || oy < -32768 || oy > 32767) {
                fe2_ini_free(&graphics); fe2_asset_fail("Invalid sprite %s", section->name); goto done;
            }
            if (snprintf(image_path, sizeof image_path, "%s/graphics/%s", data_root, image_name) >= (int)sizeof image_path ||
                fe2_read_png(image_path, &image)) {
                fe2_ini_free(&graphics); fe2_asset_fail("Could not read %s", image_path); goto done;
            }
            if (fe2_compress_sprite(image.pixels, (size_t)image.width * image.height, &packed)) {
                fe2_image_free(&image); fe2_ini_free(&graphics); goto done;
            }
            {
                int compressed = packed.count < (size_t)image.width * image.height;
                unsigned flag = ((unsigned)flags & ~2u) | (compressed ? 2u : 0u);
                const uint8_t *pixels = compressed ? packed.data : image.pixels;
                size_t pixel_n = compressed ? packed.count : (size_t)image.width * image.height;
                uint8_t header[8];
                char name[32];
                header[0] = (uint8_t)flag;
                header[1] = (uint8_t)image.height;
                header[2] = (uint8_t)image.width;
                header[3] = (uint8_t)(image.width >> 8);
                header[4] = (uint8_t)ox;
                header[5] = (uint8_t)((unsigned)ox >> 8);
                header[6] = (uint8_t)oy;
                header[7] = (uint8_t)((unsigned)oy >> 8);
                if (fe2_buf_add(&record, header, 8) || fe2_buf_add(&record, pixels, pixel_n) || record.count > 61438) {
                    fe2_buf_free(&record); fe2_buf_free(&packed); fe2_image_free(&image); fe2_ini_free(&graphics);
                    fe2_asset_fail("Graphic %d exceeds the native cache arena", ident);
                    goto done;
                }
                snprintf(name, sizeof name, "graphic/%d", ident);
                if (add_copy(tree, name, record.data, record.count)) {
                    fe2_buf_free(&record); fe2_buf_free(&packed); fe2_image_free(&image); fe2_ini_free(&graphics); goto done;
                }
            }
            fe2_buf_free(&record);
            fe2_buf_free(&packed);
            fe2_image_free(&image);
        }
        fe2_ini_free(&graphics);
    }
    {
        static const char *scores[] = {
            "hallking", "elitet", "babayaga", "barem", "jupalt", "valkries", "bluedan", "ggofkiev", "intro"
        };
        unsigned score;
        for (score = 0; score < sizeof scores / sizeof scores[0]; ++score) {
            FILE *file;
            uint8_t *data;
            long length;
            char record[64];
            if (snprintf(path, sizeof path, "%s/music/%s.mid", data_root, scores[score]) >= (int)sizeof path) goto done;
            file = fopen(path, "rb");
            if (!file) { fe2_asset_fail("Could not read %s", path); goto done; }
            if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 4 || length > 61438 || fseek(file, 0, SEEK_SET) != 0) {
                fclose(file); fe2_asset_fail("Could not read %s", path); goto done;
            }
            data = (uint8_t *)malloc((size_t)length);
            if (!data || fread(data, 1, (size_t)length, file) != (size_t)length || memcmp(data, "MThd", 4) != 0) {
                free(data); fclose(file); fe2_asset_fail("Could not read %s", path); goto done;
            }
            fclose(file);
            if (snprintf(record, sizeof record, "music/%s", scores[score]) >= (int)sizeof record) {
                free(data); goto done;
            }
            if (add_record(tree, record, data, (size_t)length)) goto done;
        }
    }
    {
        uint8_t *adlib = NULL;
        size_t adlib_n = 0;
        if (fe2_compile_adlib_image(data_root, &adlib, &adlib_n) || add_record(tree, "adlib/image", adlib, adlib_n)) goto done;
    }
    {
        char *text, *cursor;
        uint8_t *samples;
        unsigned count = 0, width = 0, height = 0, maximum = 0;
        if (snprintf(path, sizeof path, "%s/galaxy/density.pgm", data_root) >= (int)sizeof path) goto done;
        text = fe2_read_text(path, NULL);
        if (!text) { fe2_asset_fail("Could not read %s", path); goto done; }
        cursor = text;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') ++cursor;
        if (strncmp(cursor, "P2", 2) != 0) { free(text); fe2_asset_fail("Invalid galaxy density"); goto done; }
        cursor += 2;
        while (width == 0 || height == 0 || maximum == 0) {
            char *end;
            unsigned long value;
            while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') ++cursor;
            if (*cursor == '#') { while (*cursor && *cursor != '\n') ++cursor; continue; }
            value = strtoul(cursor, &end, 10);
            if (end == cursor || value > 65535) { free(text); fe2_asset_fail("Invalid galaxy density"); goto done; }
            cursor = end;
            if (width == 0) width = (unsigned)value;
            else if (height == 0) height = (unsigned)value;
            else maximum = (unsigned)value;
        }
        if (width != 128 || height != 129 || maximum != 255) {
            free(text); fe2_asset_fail("Galaxy density must be 128 by 129"); goto done;
        }
        samples = (uint8_t *)calloc(16514, 1);
        if (!samples) { free(text); goto done; }
        while (*cursor && count < 16512) {
            char *end;
            unsigned long value;
            while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') ++cursor;
            if (!*cursor) break;
            if (*cursor == '#') { while (*cursor && *cursor != '\n') ++cursor; continue; }
            value = strtoul(cursor, &end, 10);
            if (end == cursor || value > 255) { free(text); free(samples); fe2_asset_fail("Invalid galaxy density"); goto done; }
            samples[count++] = (uint8_t)value;
            cursor = end;
        }
        free(text);
        if (count != 16512 || add_record(tree, "galaxy/density", samples, 16514)) goto done;
    }
    {
        fe2_buf composites;
        memset(&composites, 0, sizeof composites);
        if (snprintf(path, sizeof path, "%s/palettes/composites.gpl", data_root) >= (int)sizeof path ||
            read_palette(path, &composites) || composites.count != 1488 ||
            add_copy(tree, "composites", composites.data, composites.count)) {
            fe2_buf_free(&composites);
            goto done;
        }
        fe2_buf_free(&composites);
    }
    {
        fe2_bank_image intro_bank;
        memset(&intro_bank, 0, sizeof intro_bank);
        if (snprintf(path, sizeof path, "%s/intro/models.ini", data_root) >= (int)sizeof path ||
            fe2_compile_bank(path, names_path[0] ? names_path : NULL, &intro_bank)) goto done;
        if (intro_bank.image_count != 27440 ||
            add_copy(tree, "intro/models", intro_bank.image, intro_bank.image_count)) {
            fe2_bank_free(&intro_bank);
            goto done;
        }
        fe2_bank_free(&intro_bank);
    }
    if (add_intro_scenes(tree, data_root)) goto done;
    {
        uint8_t *market = NULL;
        size_t market_n = 0;
        if (fe2_compile_market_image(data_root, &market, &market_n) ||
            add_record(tree, "table/market", market, market_n)) goto done;
    }
    {
        uint8_t *systems = NULL;
        size_t systems_n = 0;
        if (fe2_compile_systems_image(data_root, &systems, &systems_n) ||
            add_record(tree, "table/systems", systems, systems_n)) goto done;
    }
    {
        uint8_t *catalogue = NULL;
        size_t catalogue_n = 0;
        if (fe2_compile_catalogue(data_root, &catalogue, &catalogue_n) ||
            add_record(tree, "table/catalogue", catalogue, catalogue_n)) goto done;
    }
    {
        static const char *engine_tables[] = {
            "input", "pointer", "pool", "motion", "contact", "position", "objects", "probe"
        };
        unsigned engine;
        for (engine = 0; engine < 8; ++engine) {
            uint8_t *table = NULL;
            size_t table_n = 0;
            char record[32];
            if (fe2_compile_engine_table(data_root, engine_tables[engine], &table, &table_n) ||
                snprintf(record, sizeof record, "table/%s", engine_tables[engine]) >= (int)sizeof record ||
                add_record(tree, record, table, table_n)) goto done;
        }
    }
    {
        uint8_t *people = NULL, *panels = NULL, *intro = NULL;
        size_t people_n = 0, panels_n = 0, intro_n = 0;
        if (fe2_compile_people(data_root, &people, &people_n) ||
            add_record(tree, "table/people", people, people_n) ||
            fe2_compile_panels(data_root, &panels, &panels_n) ||
            add_record(tree, "table/panels", panels, panels_n) ||
            fe2_compile_intro_tables(data_root, &intro, &intro_n) ||
            add_record(tree, "table/intro", intro, intro_n)) goto done;
    }
    {
        uint8_t *navigation = NULL;
        size_t navigation_n = 0;
        if (fe2_compile_navigation(data_root, &navigation, &navigation_n) ||
            add_record(tree, "table/navigation", navigation, navigation_n)) goto done;
    }
    {
        uint8_t *render = NULL;
        size_t render_n = 0;
        if (fe2_compile_render(data_root, &render, &render_n) ||
            add_record(tree, "table/render", render, render_n)) goto done;
    }
    {
        uint8_t *offences = NULL, *mining = NULL;
        size_t offences_n = 0, mining_n = 0;
        if (fe2_compile_offences(data_root, &offences, &offences_n) ||
            add_record(tree, "table/offences", offences, offences_n) ||
            fe2_compile_mining(data_root, &mining, &mining_n) ||
            add_record(tree, "table/mining", mining, mining_n)) goto done;
    }
    {
        uint8_t *commander = NULL;
        size_t commander_n = 0;
        if (fe2_compile_commander(data_root, &commander, &commander_n) ||
            add_record(tree, "table/commander", commander, commander_n)) goto done;
    }
    status = 0;
done:
    if (status) fe2_tree_free(tree);
    fe2_buf_free(&font);
    fe2_buf_free(&palette);
    fe2_buf_free(&info);
    fe2_buf_free(&builtin);
    for (b = 0; b < 2; ++b) fe2_bank_free(&banks[b]);
    return status;
}

void fe2_tree_free(fe2_tree_archive *tree)
{
    size_t i;
    if (!tree) return;
    for (i = 0; i < tree->count; ++i) {
        free(tree->names[i]);
        free(tree->blobs[i]);
    }
    free(tree->names);
    free(tree->blobs);
    free(tree->parts);
    memset(tree, 0, sizeof *tree);
}
