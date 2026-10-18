/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "galactic_map_internal.h"
#include "galactic_map_tables.h"
#include <stdio.h>
#include <string.h>

static int copy_text(const fe2_map_services *game,const char *source,const fe2_call_args *args,char *out,size_t capacity)
{
    size_t n;
    if(!out || !capacity) return FE2_MAP_INVALID;
    if(game && game->expand_text) return game->expand_text(game->opaque,source,args,out,capacity);
    n=strlen(source);
    if(n>=capacity) return FE2_MAP_BUFFER_SMALL;
    memcpy(out,source,n+1);
    return 0;
}

int fe2_map_name(const fe2_map_services *game,uint16_t y,uint16_t x,uint16_t index,char *out,size_t capacity)
{
    int catalog=map_catalog(y,x);
    uint16_t a,b,mix;
    char name[16]; size_t at=0; unsigned i;
    if(catalog>=0) {
        unsigned text=map_catalogs[catalog].first+index;
        if(text>=261) return FE2_MAP_INVALID;
        return copy_text(game,map_texts[text],NULL,out,capacity);
    }
    a=(uint16_t)(y+index); b=(uint16_t)(x+a);
    a=(uint16_t)(map_rol16(a,3)+b); b=(uint16_t)(map_rol16(b,5)+a);
    b=map_rol16(b,4); mix=(uint16_t)(map_rol16(a,index&31u)+b);
    for(i=0;i<3;++i) {
        const char *part=map_syllables[(mix&124u)>>2]; size_t n=strlen(part);
        memcpy(name+at,part,n); at+=n; mix=map_rol16(mix,11);
    }
    name[0]=(char)(name[0]-32); name[at]=0;
    return copy_text(NULL,name,NULL,out,capacity);
}

int fe2_map_format_text(const fe2_map_services *game,uint16_t selector,uint16_t y,uint16_t x,
                         uint16_t index,uint32_t reference,const fe2_call_args *args,char *out,size_t capacity)
{
    selector&=0x3feu;
    if(!selector) return fe2_map_name(game,y,x,index,out,capacity);
    if(selector>=0x3fc) {
        int written;
        y=(uint16_t)((reference>>3)&65535u)>>3; x=(uint16_t)(reference>>19); index=(uint16_t)(reference&63u);
        if(selector==0x3fe) return fe2_map_name(game,y,x,index,out,capacity);
        if(!out || !capacity) return FE2_MAP_INVALID;
        written=snprintf(out,capacity," (%d,%d)",map_s16((uint16_t)(y-0x1718u)),map_s16((uint16_t)(x-0x1524u)));
        return written<0 || (size_t)written>=capacity ? FE2_MAP_BUFFER_SMALL : 0;
    }
    index=(uint16_t)(selector/2-1);
    if(index>=261) return FE2_MAP_INVALID;
    return copy_text(game,map_texts[index],args,out,capacity);
}

int fe2_map_distance(const fe2_map_services *game,const fe2_map_position *from,const fe2_map_position *to,uint32_t *distance)
{
    uint32_t differences[3]; int16_t reduced[3]; uint16_t length;
    unsigned scale=14,i;
    if(!from || !to || !distance) return FE2_MAP_INVALID;
    differences[0]=map_sar(from->y-to->y,8); differences[1]=map_sar(from->x-to->x,8); differences[2]=map_sar(from->z-to->z,8);
    for(;;) {
        int fits=1;
        for(i=0;i<3;++i) if(map_s32(differences[i])<-32768 || map_s32(differences[i])>32767) fits=0;
        if(fits) break;
        for(i=0;i<3;++i) differences[i]=map_sar(differences[i],1);
        --scale;
    }
    for(i=0;i<3;++i) reduced[i]=(int16_t)map_s16(differences[i]);
    MAP_TRY(MAP_CALL(game,vector_length,reduced,&length));
    *distance=((uint32_t)length*0xc8b4u)>>scale;
    return 0;
}

uint32_t map_interpolate(uint16_t descriptor,int query)
{
    uint8_t kind=(uint8_t)(query ? descriptor : descriptor>>8);
    const map_value_window *window=query ? &map_query_values[kind&127u] : &map_selected_values[kind&127u];
    uint32_t fraction=(query ? descriptor>>8 : descriptor)&248u;
    return window->base+(((window->next-window->base)>>16)*(fraction<<8));
}

int fe2_map_find_candidate(fe2_map_state *state,const fe2_map_services *game,uint32_t reference,
                            unsigned offset_table,fe2_map_candidate *candidate)
{
    uint32_t base=reference; uint16_t y,x,count,choice; int catalog;
    if(!state || !candidate || offset_table>2) return FE2_MAP_INVALID;
    if(offset_table) {
        MAP_TRY(MAP_CALL(game,random_below,12,&choice));
        if(choice>=12) return FE2_MAP_INVALID;
        base+=(uint32_t)((int32_t)map_candidate_offsets[offset_table-1][choice]*64);
    }
    base&=UINT32_C(0xffffffc0);
    y=(uint16_t)((base>>6)&8191u); x=(uint16_t)(base>>19);
    catalog=map_catalog(y,x);
    if(catalog>=0) count=map_catalogs[catalog].count;
    else { MAP_TRY(fe2_map_density(game,y,x,0,&count)); count>>=10; }
    if(!count) { *candidate=(fe2_map_candidate){0,0}; return 0; }
    MAP_TRY(MAP_CALL(game,random_below,count,&choice));
    candidate->reference=base+choice; candidate->available=candidate->reference!=reference;
    if(!candidate->available) candidate->reference=0;
    return 0;
}

int fe2_map_query_system(fe2_map_state *state,const fe2_map_services *game,uint32_t reference,
                          unsigned mode,fe2_map_query *out)
{
    uint16_t y=(uint16_t)((reference>>3)&65535u)>>3,x=(uint16_t)(reference>>19);
    fe2_map_projection projected={{mode,((uint32_t)x<<16)|y,reference>>3},0};
    fe2_map_scene scene; fe2_map_details details={0}; fe2_map_query result={0};
    unsigned index=reference&63u;
    if(!state || !out || mode<2) return FE2_MAP_INVALID;
    state->display_flags&=(uint16_t)~128u;
    MAP_TRY(fe2_map_build_sector(state,game,y,x,&projected,&scene));
    result.reference=reference; result.index=(uint16_t)index;
    if(index>=scene.count) { *out=result; return 0; }
    result.available=1; result.position=fe2_map_decode_record(y,x,&scene.records[index]);
    result.descriptor=map_swap16(scene.identifiers[index]);
    if(mode==2) MAP_TRY(fe2_map_distance(game,&result.position,&state->home,&result.distance));
    else {
        MAP_TRY(fe2_map_name(game,y,x,(uint16_t)index,result.name,sizeof result.name));
        MAP_TRY(MAP_CALL(game,system_details,reference,0,&details));
        /* Preserve the original crossed scratch mapping as arithmetic values. */
        result.coordinate_mix=result.position.y+((result.position.x<<16)|(result.position.x>>16))-result.position.z;
        if(result.descriptor&128u) result.coordinate_mix=0;
        result.table_value=map_interpolate(result.descriptor,1);
        result.classification=(uint8_t)((result.descriptor>>8)&7u);
        result.status=details.status;
    }
    *out=result;
    return 0;
}

void fe2_map_cached_selection(const fe2_map_state *state,fe2_map_query *result)
{
    memset(result,0,sizeof *result); result->available=1;
    result->reference=state->selection_reference; result->distance=state->selection_distance;
    result->coordinate_mix=state->selection_mix; result->table_value=state->selection_value;
    result->classification=(uint8_t)(state->selected_descriptor&7u); result->status=state->detail_status;
    result->position=state->selected; result->descriptor=state->selected_descriptor; result->index=state->selected_index;
    memcpy(result->name,state->selected_name,sizeof result->name);
}
