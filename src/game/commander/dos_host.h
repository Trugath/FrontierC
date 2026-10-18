/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_COMMANDER_DOS_HOST_H
#define FE2_COMMANDER_DOS_HOST_H

/*
 * A host implementation of fe2_dos_platform: one DOS drive mapped to a host
 * directory, whose current directory is its root. Paths are rooted DOS
 * paths on that drive ("C:\\COMMANDR\\NAME") or relative to the root;
 * components match host entries without case, and searches return upper-
 * case names that fit DOS 8.3. "." and ".." components and other drives
 * are rejected (errors 3 and 15). This is a file platform, not a DOS
 * emulator: there are no device names, attributes or sharing modes.
 */
#include <stdio.h>

#include "preferences.h"

enum { FE2_DOS_HOST_HANDLES = 16, FE2_DOS_HOST_PATH_BYTES = 1024 };

typedef struct fe2_dos_host {
    char root[FE2_DOS_HOST_PATH_BYTES];
    char drive;
    char current[64];                 /* always the root ("") */
    FILE *files[FE2_DOS_HOST_HANDLES];
    /* The active search. */
    void *search;
    char search_directory[FE2_DOS_HOST_PATH_BYTES];
    char search_name[FE2_DOS_NAME_BYTES];
    uint16_t search_attributes;
} fe2_dos_host;

/* Returns 0, or -1 for an unusable root or drive. */
int fe2_dos_host_init(fe2_dos_host *host, const char *root, char drive);
void fe2_dos_host_close(fe2_dos_host *host);
fe2_dos_platform fe2_dos_host_platform(fe2_dos_host *host);

#endif
