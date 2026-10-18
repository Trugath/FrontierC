/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_ASSET_ADLIB_H
#define FE2_ASSET_ADLIB_H

#include <stddef.h>
#include <stdint.h>

/* Write patches.csv, records.tsv, and pitch_curves.csv from an AdLib image. */
int fe2_extract_adlib(const uint8_t *image, size_t image_n, const char *data_root);

/* Compile data/adlib into the driver image the executable used to embed. */
int fe2_compile_adlib_image(const char *data_root, uint8_t **out, size_t *out_n);

#endif
