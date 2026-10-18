/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
#include "resident.h"
#include <string.h>

void fe2_resident_init(fe2_resident *r)
{
    memset(r, 0, sizeof(*r));
}
