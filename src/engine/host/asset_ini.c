/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "asset_ini.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_text(const char *text, size_t count)
{
    char *copy = (char *)malloc(count + 1);
    if (!copy) return NULL;
    memcpy(copy, text, count);
    copy[count] = 0;
    return copy;
}

static int add_key(fe2_ini_section *section, const char *key, size_t key_n, const char *value, size_t value_n)
{
    char **keys = (char **)realloc(section->key, (section->count + 1) * sizeof *keys);
    char **values;
    char *name, *text;
    size_t i;
    if (!keys) return -1;
    section->key = keys;
    values = (char **)realloc(section->value, (section->count + 1) * sizeof *values);
    if (!values) return -1;
    section->value = values;
    name = dup_text(key, key_n);
    text = dup_text(value, value_n);
    if (!name || !text) { free(name); free(text); return -1; }
    for (i = 0; i < key_n; ++i) name[i] = (char)tolower((unsigned char)name[i]);
    section->key[section->count] = name;
    section->value[section->count] = text;
    ++section->count;
    return 0;
}

void fe2_ini_free(fe2_ini *ini)
{
    size_t s, k;
    if (!ini) return;
    for (s = 0; s < ini->count; ++s) {
        for (k = 0; k < ini->sections[s].count; ++k) {
            free(ini->sections[s].key[k]);
            free(ini->sections[s].value[k]);
        }
        free(ini->sections[s].key);
        free(ini->sections[s].value);
        free(ini->sections[s].name);
    }
    free(ini->sections);
    memset(ini, 0, sizeof *ini);
}

int fe2_ini_load(fe2_ini *ini, const char *path)
{
    FILE *file;
    char line[8192];
    fe2_ini_section *current = NULL;
    size_t value_index = 0;
    if (!ini || !path) return -1;
    memset(ini, 0, sizeof *ini);
    file = fopen(path, "rb");
    if (!file) return -1;
    while (fgets(line, sizeof line, file)) {
        size_t length = strlen(line);
        char *start;
        if (length && line[length - 1] != '\n' && !feof(file)) { fclose(file); fe2_ini_free(ini); return -1; }
        if (length && line[length - 1] == '\n') line[--length] = 0;
        if (length && line[length - 1] == '\r') line[--length] = 0;
        if (current && current->count && (line[0] == ' ' || line[0] == '\t')) {
            char *value = current->value[value_index];
            size_t old = strlen(value);
            char *grown = (char *)realloc(value, old + 1 + length + 1);
            if (!grown) { fclose(file); fe2_ini_free(ini); return -1; }
            grown[old] = '\n';
            memcpy(grown + old + 1, line, length + 1);
            current->value[value_index] = grown;
            continue;
        }
        start = line;
        while (*start == ' ' || *start == '\t') ++start;
        if (!*start || *start == '#' || *start == ';') continue;
        if (*start == '[') {
            char *end = strchr(start, ']');
            fe2_ini_section *grown;
            if (!end || end == start + 1) { fclose(file); fe2_ini_free(ini); return -1; }
            grown = (fe2_ini_section *)realloc(ini->sections, (ini->count + 1) * sizeof *grown);
            if (!grown) { fclose(file); fe2_ini_free(ini); return -1; }
            ini->sections = grown;
            current = &ini->sections[ini->count];
            memset(current, 0, sizeof *current);
            current->name = dup_text(start + 1, (size_t)(end - start - 1));
            if (!current->name) { fclose(file); fe2_ini_free(ini); return -1; }
            ++ini->count;
            continue;
        }
        if (!current) { fclose(file); fe2_ini_free(ini); return -1; }
        {
            char *eq = strchr(start, '=');
            char *key_end, *value;
            if (!eq) { fclose(file); fe2_ini_free(ini); return -1; }
            key_end = eq;
            while (key_end > start && (key_end[-1] == ' ' || key_end[-1] == '\t')) --key_end;
            value = eq + 1;
            while (*value == ' ' || *value == '\t') ++value;
            if (add_key(current, start, (size_t)(key_end - start), value, strlen(value))) {
                fclose(file); fe2_ini_free(ini); return -1;
            }
            value_index = current->count - 1;
        }
    }
    if (ferror(file)) { fclose(file); fe2_ini_free(ini); return -1; }
    fclose(file);
    return 0;
}

fe2_ini_section *fe2_ini_section_find(fe2_ini *ini, const char *name)
{
    size_t i;
    if (!ini || !name) return NULL;
    for (i = 0; i < ini->count; ++i)
        if (strcmp(ini->sections[i].name, name) == 0) return &ini->sections[i];
    return NULL;
}

const char *fe2_ini_get(const fe2_ini_section *section, const char *key)
{
    size_t i;
    if (!section || !key) return NULL;
    for (i = 0; i < section->count; ++i)
        if (strcmp(section->key[i], key) == 0) return section->value[i];
    return NULL;
}
