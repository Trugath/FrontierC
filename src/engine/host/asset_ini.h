/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_ASSET_INI_H
#define FE2_ASSET_INI_H

#include <stddef.h>

typedef struct fe2_ini_section {
    char *name;
    char **key;
    char **value;
    size_t count;
} fe2_ini_section;

typedef struct fe2_ini {
    fe2_ini_section *sections;
    size_t count;
} fe2_ini;

int fe2_ini_load(fe2_ini *ini, const char *path);
void fe2_ini_free(fe2_ini *ini);
fe2_ini_section *fe2_ini_section_find(fe2_ini *ini, const char *name);
const char *fe2_ini_get(const fe2_ini_section *section, const char *key);

#endif
