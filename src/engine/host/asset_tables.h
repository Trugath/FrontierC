/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_ASSET_TABLES_H
#define FE2_ASSET_TABLES_H

#include <stddef.h>
#include <stdint.h>

/* Compile the market image that frontier.dat stores as table/market.
 * The bytes are the 0xa2..0x6eb span, not the zero-padded 64 KiB image. */
int fe2_compile_market_image(const char *data_root, uint8_t **out, size_t *out_n);
/* Systems image 0xa2..0x3f21, stored as table/systems. */
int fe2_compile_systems_image(const char *data_root, uint8_t **out, size_t *out_n);
/* 331 catalogue records, 48 bytes each, stored as table/catalogue. */
int fe2_compile_catalogue(const char *data_root, uint8_t **out, size_t *out_n);
/* One plain engine table: input, pointer, pool, motion, contact, position,
 * objects, or probe. The bytes are the table the game indexes. */
int fe2_compile_engine_table(const char *data_root, const char *name, uint8_t **out, size_t *out_n);
/* People template followed by the creation tables. */
int fe2_compile_people(const char *data_root, uint8_t **out, size_t *out_n);
/* Panel image from 0x9e through 0xfb. */
int fe2_compile_panels(const char *data_root, uint8_t **out, size_t *out_n);
/* Intro scene windows, orbit interval, render vector, and classification. */
int fe2_compile_intro_tables(const char *data_root, uint8_t **out, size_t *out_n);
/* Navigation image 0xa2..0x10c. */
int fe2_compile_navigation(const char *data_root, uint8_t **out, size_t *out_n);
/* Render materials and the other render tables read from data. The
 * arccosine lookup stays compiled into the executable. */
int fe2_compile_render(const char *data_root, uint8_t **out, size_t *out_n);
/* Ownership challenges followed by the eleven offence rows. */
int fe2_compile_offences(const char *data_root, uint8_t **out, size_t *out_n);
/* Mining yields followed by the category-to-commodity map. */
int fe2_compile_mining(const char *data_root, uint8_t **out, size_t *out_n);
/* Commander preferences, widgets, presets, and file literals. */
int fe2_compile_commander(const char *data_root, uint8_t **out, size_t *out_n);

#endif
