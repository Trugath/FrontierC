/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "galactic_map_internal.h"
#include "galactic_map_tables.h"

typedef struct layout {
    uint16_t previous_spacing,x_mask,y_mask,x,y,selection;
} layout;
typedef struct hierarchy {
    const fe2_map_services *game;
    const fe2_record_space *records;
    unsigned cursor;
    layout node;
} hierarchy;

static int visit(hierarchy *h,uint16_t parent,int force);

static void restore(hierarchy *h,layout saved)
{ uint16_t selection=h->node.selection; h->node=saved; h->node.selection=selection; }

static int draw_node(hierarchy *h,unsigned kind)
{
    uint16_t spacing,delta,x,y;
    if(kind>0x44) return FE2_MAP_INVALID;
    spacing=map_node_spacing[kind];
    delta=(uint16_t)(h->node.previous_spacing+spacing+3);
    h->node.x=(uint16_t)(h->node.x+(delta&h->node.x_mask));
    h->node.y=(uint16_t)(h->node.y+(delta&h->node.y_mask));
    x=h->node.x; y=h->node.y;
    MAP_TRY(MAP_CALL(h->game,hierarchy_node,h->node.selection,(uint16_t)(spacing+1),&x,&y));
    h->node.previous_spacing=spacing;
    return MAP_CALL(h->game,draw_body_sprite,map_sprites[kind],(uint16_t)(x-map_sprite_x[kind]),(uint16_t)(y-map_sprite_y[kind]));
}

static int visit(hierarchy *h,uint16_t parent,int force)
{
    while(h->cursor<h->records->count) {
        const fe2_system_record *record=&h->records->records[h->cursor];
        uint16_t kind=fe2_record_word(record,FE2_RECORD_KIND),p=fe2_record_word(record,FE2_RECORD_PARENT);
        if(!fe2_record_dword(record,FE2_RECORD_SIZE_MANTISSA)) return force ? FE2_MAP_INVALID : 0;
        if(!force && kind!=FE2_RECORD_KIND_VERBATIM && p!=parent) {
            layout saved;
            if(map_s16(p)<map_s16(parent)) return 0;
            saved=h->node;
            h->node.x_mask=saved.y_mask; h->node.y_mask=saved.x_mask;
            MAP_TRY(visit(h,p,0)); restore(h,saved); continue;
        }
        if(force || kind!=FE2_RECORD_KIND_VERBATIM) MAP_TRY(draw_node(h,kind));
        force=0; ++h->node.selection; ++h->cursor;
        if(kind) continue;
        {
            layout saved=h->node;
            if(h->cursor+1>=h->records->count) return FE2_MAP_INVALID;
            h->node.x_mask=0; h->node.y_mask=0; h->node.y=(uint16_t)(h->node.y-29);
            MAP_TRY(visit(h,127,1)); h->node.y=(uint16_t)(h->node.y+57);
            MAP_TRY(visit(h,127,1)); restore(h,saved);
        }
        if(h->cursor>=h->records->count || !fe2_record_dword(&h->records->records[h->cursor],0)) return 0;
        p=fe2_record_word(&h->records->records[h->cursor],FE2_RECORD_PARENT);
        if(p==parent) continue;
        if(map_s16(p)<map_s16(parent)) return 0;
        {
            layout saved=h->node;
            int relation=map_s16((uint16_t)(h->cursor-1-p));
            if(relation>0) {
                h->node.x_mask=saved.y_mask; h->node.y_mask=saved.x_mask;
                MAP_TRY(visit(h,p,0));
            } else {
                h->node.x_mask=65535; h->node.y_mask=0;
                h->node.x=(uint16_t)(h->node.x-25);
                h->node.y=(uint16_t)(h->node.y+(relation==0 ? -29 : 28));
                MAP_TRY(visit(h,127,1));
            }
            restore(h,saved);
        }
    }
    return 0;
}

int fe2_map_draw_hierarchy(fe2_map_state *state,const fe2_map_services *game)
{
    hierarchy h;
    if(!state || !state->bodies.records || !state->bodies.count) return FE2_MAP_INVALID;
    MAP_TRY(MAP_SIMPLE(game,show_panel)); MAP_TRY(MAP_SIMPLE(game,clear_panel));
    h.game=game; h.records=&state->bodies; h.cursor=0;
    h.node=(layout){0,0,65535,25,1,128};
    return visit(&h,0,0);
}
