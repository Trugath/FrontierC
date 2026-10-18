/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_ASSET_BIND_H
#define FE2_ASSET_BIND_H

#include "asset_archive.h"
#include "render/runtime.h"

/* Point the runtime resources at records inside the archive.
 * Pass graphics as NULL on the first call, then the runtime cache. */
int fe2_asset_archive_apply(const fe2_asset_archive *archive, fe2_render_runtime_resources *resources,
                            fe2_graphics_cache *graphics);

/* Point compiled gameplay tables at soundtrack, density, prose, and intro records. */
int fe2_asset_bind_install(const fe2_asset_archive *archive);

#endif
