/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "galactic_map_internal.h"
#include "galactic_map_tables.h"
#include "../world/density_grid.h"
#include <string.h>

uint32_t fe2_map_next_seed(fe2_map_seed *seed)
{
    uint32_t total=seed->a+seed->b;
    seed->a=((seed->a<<3)|(seed->a>>29))+total;
    seed->b=(total<<5)|(total>>27);
    return seed->a;
}

int map_catalog(uint16_t y,uint16_t x)
{
    int i;
    for(i=46;i>=0;--i) if(map_catalogs[i].y==y && map_catalogs[i].x==x) return i;
    return -1;
}

int fe2_map_density(const fe2_map_services *game,uint16_t y,uint16_t x,
                     uint16_t count,uint16_t *output)
{
    fe2_map_samples s;
    unsigned ry=y&63u,rx=x&63u,index;
    uint32_t density,weight;
    if(!output || count&32768u) return FE2_MAP_INVALID;
    if(y>=8192 || x>=8192) { *output=0; return 0; }
    if(game && game->density_samples) MAP_TRY(game->density_samples(game->opaque,y,x,&s));
    else {
        fe2_density_samples native;
        MAP_TRY(fe2_density_neighbors(y,x,&native));
        s.a=native.a; s.b=native.b; s.c=native.c; s.d=native.d;
    }
    /* Original X/Y sampler order is Y first (horizontal), X second (vertical). */
    density=((uint32_t)s.a*(64-ry)*(64-rx)+(uint32_t)s.b*ry*(64-rx)+
             (uint32_t)s.c*(64-ry)*rx+(uint32_t)s.d*ry*rx)/16u;
    if(count<18) {
        index=(((uint16_t)(ry*512u+density)^(8u*ry*rx))>>4)&254u;
        weight=map_weights[index/2];
        if(count) weight=(uint16_t)~((weight*map_count_weights[count])>>16);
        density=(density*weight)>>16;
    }
    *output=(uint16_t)density;
    return 0;
}

int fe2_map_build_sector(fe2_map_state *state,const fe2_map_services *game,
                          uint16_t y,uint16_t x,const fe2_map_projection *projected,fe2_map_scene *scene)
{
    int catalog;
    unsigned i,n;
    uint16_t density;
    if(!state || !projected || !scene) return FE2_MAP_INVALID;
    scene->sector_y=y; scene->sector_x=x; scene->flags=projected->flags;
    memcpy(scene->position,projected->position,sizeof scene->position);
    scene->position[2]+=state->depth;
    scene->count=0; scene->draw_kind=4;
    scene->has_origin=0; scene->origin=(fe2_map_record){0,9,0,0};
    catalog=map_catalog(y,x);
    if(catalog>=0) {
        const map_catalog_entry *entry=&map_catalogs[catalog];
        if(entry->count>FE2_MAP_MAX_SYSTEMS || entry->first+entry->count>117) return FE2_MAP_INVALID;
        scene->count=entry->count;
        scene->has_origin=scene->count!=0;
        for(i=0;i<scene->count;++i) {
            scene->records[i]=map_catalog_records[entry->first+i];
            scene->identifiers[i]=map_swap16(map_catalog_identifiers[entry->first+i]);
        }
        if(scene->count && (state->display_flags&128u)) MAP_TRY(MAP_CALL(game,draw_scene,scene,0));
        /* The original draws this extra even for an empty catalogue. */
        if(entry->extra && (!scene->count || (state->display_flags&128u))) {
            scene->draw_kind=entry->extra; MAP_TRY(MAP_CALL(game,draw_scene,scene,1));
        }
        return 0;
    }
    MAP_TRY(fe2_map_density(game,y,x,0,&density));
    n=density>>10;
    if(!n) return 0;
    if(n>62) n=62;
    state->seed.a=((uint32_t)y<<16)|x;
    state->seed.b=((uint32_t)x<<16)|y;
    for(i=0;i<3;++i) fe2_map_next_seed(&state->seed);
    scene->count=n;
    scene->has_origin=1;
    for(i=0;i<n;++i) {
        uint32_t value=fe2_map_next_seed(&state->seed);
        uint16_t plane=(uint16_t)value;
        uint8_t low;
        plane=(uint16_t)((plane>>1)|(plane&32768u));
        low=(uint8_t)plane;
        low=(uint8_t)((low>>1)|(low&128u));
        plane=(uint16_t)((plane&0xff00u)|low);
        scene->records[i].depth=(uint8_t)(value>>16);
        scene->records[i].style=9;
        scene->records[i].y=(uint8_t)plane;
        scene->records[i].x=(uint8_t)(plane>>8);
        scene->identifiers[i]=(uint16_t)(map_identifier_low[state->seed.b&31u]|
            ((unsigned)map_identifier_high[(state->seed.b>>16)&31u]<<8));
    }
    if(state->display_flags&128u) MAP_TRY(MAP_CALL(game,draw_scene,scene,0));
    return 0;
}

fe2_map_position fe2_map_decode_record(uint16_t y,uint16_t x,const fe2_map_record *record)
{
    fe2_map_position result;
    int depth=map_s8((0u-record->depth)&255u);
    result.y=((uint32_t)y<<16)|(uint16_t)(((unsigned)record->y<<9)+0x8000u);
    result.x=((uint32_t)x<<16)|(uint16_t)(((unsigned)record->x<<9)+0x8000u);
    result.z=(uint32_t)(-512*depth-(depth<0 ? 511 : 0));
    return result;
}

int fe2_map_nearest(const fe2_map_scene *scene,uint8_t y,uint8_t x)
{
    unsigned i,best=32; int winner=-1;
    if(!scene || scene->count>FE2_MAP_MAX_SYSTEMS) return -1;
    for(i=0;i<scene->count;++i) {
        int dy=map_s8((scene->records[i].y-y)&255u),dx=map_s8((scene->records[i].x-x)&255u);
        unsigned distance=(unsigned)((dy<0 ? -dy : dy)+(dx<0 ? -dx : dx))&255u;
        if(distance<best) { best=distance; winner=(int)i; }
    }
    return winner;
}
