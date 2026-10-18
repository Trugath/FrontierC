/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_GALACTIC_MAP_INTERNAL_H
#define FE2_GALACTIC_MAP_INTERNAL_H
#include "galactic_map.h"
#define MAP_TRY(expr) do { int status_=(expr); if(status_) return status_; } while(0)
#define MAP_CALL(game,member,...) ((game) && (game)->member ? (game)->member((game)->opaque,__VA_ARGS__) : FE2_MAP_MISSING_SERVICE)
#define MAP_SIMPLE(game,member) ((game) && (game)->member ? (game)->member((game)->opaque) : FE2_MAP_MISSING_SERVICE)
static inline int map_s16(uint32_t v) { v&=65535u; return v&32768u ? (int)v-65536 : (int)v; }
static inline int map_s8(unsigned v) { v&=255u; return v&128u ? (int)v-256 : (int)v; }
static inline int64_t map_s32(uint32_t v) { return v&UINT32_C(0x80000000) ? (int64_t)v-INT64_C(4294967296) : v; }
static inline uint32_t map_sar(uint32_t v,unsigned n)
{ return n ? (v>>n)|((v&UINT32_C(0x80000000)) ? UINT32_MAX<<(32-n) : 0u) : v; }
static inline uint16_t map_rol16(uint16_t v,unsigned n)
{ n&=15u; return n ? (uint16_t)((v<<n)|(v>>(16-n))) : v; }
static inline uint16_t map_swap16(uint16_t v) { return (uint16_t)((v<<8)|(v>>8)); }
int map_catalog(uint16_t y,uint16_t x);
uint32_t map_interpolate(uint16_t descriptor,int query);
int map_text(const fe2_map_services *,uint16_t selector,const fe2_call_args *,uint8_t colour,uint16_t x,uint16_t y);
int map_resident(const fe2_map_services *,uint16_t selector,const fe2_call_args *,uint8_t colour,uint16_t x,uint16_t y);
int map_button(const fe2_map_services *,unsigned button,unsigned x,unsigned y);
int map_reset_menu(fe2_map_state *,const fe2_map_services *);
int map_density_pass(fe2_map_state *,const fe2_map_services *,unsigned step,int *interrupted);
int map_density_annotations(fe2_map_state *,const fe2_map_services *);
int map_system_text(fe2_map_state *,const fe2_map_services *);
int map_project(const fe2_map_services *,const int16_t matrix[9],const int16_t vector[3],uint32_t projected[3]);
#endif
